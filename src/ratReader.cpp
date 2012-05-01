// NDK:
#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/ARRAY.h"
#include "DDImage/Thread.h"
//HDK:
#include "IMG/IMG_File.h"
#include "IMG/IMG_Stat.h"
#include "IMG/IMG_FileParms.h"
#include "PXL/PXL_Raster.h"
#include "IMG/IMG_FileTypes.h"
#include "UT/UT_WorkBuffer.h"
#include "UT/UT_Options.h"

using namespace DD::Image;

class ratReaderFormat : public ReaderFormat
{
    friend class ratReader;
    bool _use_scanline_engine;
    bool _reverse_scanlines;
public:
    bool use_scanline_engine() const
    {
        return _use_scanline_engine;
    }
    bool reverse_scanlines() const
    {
        return _reverse_scanlines;
    }
    ratReaderFormat()
    {
        _use_scanline_engine = true;
        _reverse_scanlines   = true;
    }
    void knobs(Knob_Callback c)
    {
        Bool_knob(c, &_use_scanline_engine, "use_scanline_engine", "use scanline engine");
        Tooltip(c, "Use scanlines instead of full raster pre-load to read data from a *.rat file. "
                "Full raster should perform faster for the expense of memory consumption.");
        Bool_knob(c, &_reverse_scanlines, "reverse_scanlines", "reverse scanlines");
        Tooltip(c, "Reverse the order of the scanlines from a rat image.");
    }

    void append(Hash& hash)
    {
        hash.append(_use_scanline_engine);
    }
};

class ratReader : public Reader
{
public:
    const MetaData::Bundle& fetchMetaData(const char* key)
    {
        return _meta;
    }
    ratReader(Read*, int);
    ~ratReader();
    void          open();
    void          engine(int y, int x, int r, ChannelMask, Row &);
    void   raster_engine(int y, int x, int r, ChannelMask, Row &);
    void scanline_engine(int y, int x, int r, ChannelMask, Row &);    
    void lookupChannels(std::set<Channel>& channel, const char* name);
    static const Reader::Description d;

private:
    IMG_File *rat;
    IMG_FileParms *parms;
    UT_PtrArray<PXL_Raster *> images;

    void *buffer;
    bool loaded;
    int  depth, xres, yres;
    bool use_scanline_engine;
    bool reverse_scanlines;
    std::map<Channel, const char*>          channel_map;
    std::map<Channel, std::pair<int, int> > rat_chan_index;

    MetaData::Bundle _meta;
    Lock lock;
};

static bool 
test(int fd, const unsigned char* block, int length)
{
    return block[0] == 'f' && block[1] == 'b' && block[2] == 't' && block[3] == 'H';
}

static 
Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
    return new ratReader(iop, fd);
}

static 
ReaderFormat* buildformat(Read* iop)
{
  return new ratReaderFormat();
}

const 
Reader::Description ratReader::d("rat\0", build, test, buildformat);

void 
ratReader::lookupChannels(std::set<Channel>& channel, const char* name)
{
    if (strcmp(name, "y") == 0 || strcmp(name, "Y") == 0) 
    {
        channel.insert(Chan_Red);
        if (!iop->raw()) 
        {
            channel.insert(Chan_Green);
            channel.insert(Chan_Blue);
        }
    }
    else if (strcmp(name, "C.red") == 0)
        channel.insert(Chan_Red);
    else if (strcmp(name, "C.green") == 0)
        channel.insert(Chan_Green);
    else if (strcmp(name, "C.blue") == 0)
        channel.insert(Chan_Blue);
    else if (strcmp(name, "C.alpha") == 0)
        channel.insert(Chan_Alpha);
    else if (strcmp(name, "Pz.red") == 0)
        channel.insert(Chan_Z);
    else
    {
        Channel ch = getChannel(name);
        channel.insert(ch);
    }
}

ratReader::ratReader(Read *r, int fd): Reader(r)
{
    // Init: 
    buffer = NULL;
    loaded = false;
    rat    = NULL;
    use_scanline_engine = true;
    parms = new IMG_FileParms();
    
    // Read knobs:
    ratReaderFormat* rrf = dynamic_cast<ratReaderFormat*>(r->handler());
    if (rrf)
    {
        if (rrf->reverse_scanlines())
            parms->orientImage(IMG_ORIENT_LEFT_FIRST, IMG_ORIENT_TOP_FIRST);

        use_scanline_engine = rrf->use_scanline_engine(); 
    }
    else
        parms->orientImage(IMG_ORIENT_LEFT_FIRST, IMG_ORIENT_Y_NONE);

    // Set some of the settings, which are assumed currently by both ::engines. 
    // Leaving them as they are would require some serious switches in logic bellow.
    parms->setDataType(IMG_FLOAT);
    parms->setInterleaved(IMG_INTERLEAVED);
    parms->setComponentOrder(IMG_COMPONENT_RGBA);
    
    // Create and open rat file, get stats:
    rat = IMG_File::open(r->filename(), parms);

    // TODO: Meta data attempt:
    #if defined(DEBUG)
    UT_String info;
    rat->getAdditionalInfo(info);
    iop->warning("Rat info: %s\n", info.buffer());
    for (int i = 0; i < rat->getNumOptions(); i++)
    {
        iop->warning("%s: %s\n", rat->getOptionName(i), rat->getOptionValue(i));
    }
    UT_SharedPtr<UT_Options> opt;
    UT_WorkBuffer wbuf;
    if (opt = rat->imageTextureOptions())
    {
        wbuf.strcpy("DSM Options: ");
        opt->appendPyDictionary(wbuf);
        iop->warning("Options %i: %s\n", opt->getNumOptions(), wbuf.buffer());
    }
    #endif
    if (!rat)
    {
        iop->error("Failed to open .rat file.");
    }
    const IMG_Stat &stat = rat->getStat();
    depth = 0;
    #if defined(DEBUG)
    iop->warning("Rat opened: %s", r->filename());
    #endif

    // Since RAT can store varying bitdepth per plane, pixel-byte algebra doesn't 
    // help in finding out a number of channels. We need to iterate over planes. 
    for (int i = 0; i < stat.getNumPlanes(); i++)
    {
        IMG_Plane *plane = stat.getPlane(i);

        // The easiest yet not unequivocal way to determine 2d versus deep RAT files:
        if (!strcmp(plane->getName(), "Depth-Complexity"))
            iop->warning("This seems to be deep shadow/camera map. ratReader will treat it as a 2d raster image ignoring deep pixels.");

        #if defined(DEBUG)
        iop->warning("Plane name: %s", plane->getName());
        #endif
        depth += IMGvectorSize(plane->getColorModel()); 
    } 

    ChannelSet mask;
    for (int i = 0; i < stat.getNumPlanes(); i++)
    {
        IMG_Plane *plane = stat.getPlane(i);
        const int  nchan = IMGvectorSize(plane->getColorModel());

        for (int j = 0; j < nchan; j++)
        {
            std::string chan = std::string(plane->getComponentName(j) ? plane->getComponentName(j): "r"); 

            if      (chan == "r") chan = "red"; 
            else if (chan == "g") chan = "green";
            else if (chan == "b") chan = "blue"; 
            else if (chan == "a") chan = "alpha";

            std::string chan_name = std::string(plane->getName()) + std::string(".") + chan;
            std::set<Channel> channels;
            lookupChannels(channels, chan_name.c_str());

            if (!channels.empty()) 
            {
                for (std::set<Channel>::iterator it = channels.begin(); it != channels.end(); it++) 
                {
                    Channel channel = *it;
                    channel_map[channel]= chan_name.c_str();
                    std::pair<int, int> idx(i, j);
                    rat_chan_index[channel] = idx;
                     
                    #if defined(DEBUG)
                    iop->warning("Rat %s (%i,%i) becomes %s", chan_name.c_str(), i, j, getName(channel));
                    #endif
                    mask += channel;
                }
            }
            else
                iop->warning("Can't create a channel from %s", chan_name.c_str());
        }
    }
    // Set info:
    #if defined(DEBUG)
    iop->warning("Channel number: %i", depth);
    #endif
    set_info(stat.getDataWidth(), stat.getDataHeight(), depth);
    info_.channels(mask);
}

void
ratReader::open()
{
    if (!rat)
        iop->error("Rat is not opened.");

    if (!use_scanline_engine)
    {
        #if defined(DEBUG)
        iop->warning("About to load images: %s", rat->getStat().getFilename().buffer());
        #endif

        loaded = rat->readImages(images);
        if(!loaded)
            iop->error("Can't load data from image: %s", rat->getStat().getFilename().buffer());
        else
        {
            #if defined(DEBUG)
            iop->warning("Image loaded: %s", rat->getStat().getFilename().buffer());
            #endif
        }
    }    
}

void 
ratReader::engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{
    if (use_scanline_engine)
        scanline_engine(y, x, xr, channels, row);
    else
    {
        if (!loaded)
            this->open();    
        raster_engine(y, x, xr, channels, row);
    }
}


void 
ratReader::raster_engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{ 
    lock.lock();
    int Y = height() - y - 1;
    row.range(0, width());        
 
    if (rat && loaded)
    {
        const IMG_Stat &stat = rat->getStat();
        #if defined(DEBUG)
        iop->warning("Raster engine active.");
        #endif
        foreach(z, channels)
        {
            if (rat_chan_index.count(z) == 0)
            {
                #if defined(DEBUG)
                iop->error("%s can't be found in rat_chan_index", getName(z));
                #endif
                continue;
            }
            int rindex  = rat_chan_index[z].first;
            int color   = rat_chan_index[z].second;
            int ncolors = IMGvectorSize((stat.getPlane(rindex))->getColorModel());
            PXL_Raster *raster  = images(rindex);

            if (!raster)
            {   
                iop->error("Can't allocate the raster? %i", rindex);
                continue;
            }
                
            #if defined(DEBUG)
            iop->warning("%s.%s writes to: %s. Interleaved: %i, colors: %i", (stat.getPlane(rindex))->getName(), \
                        (stat.getPlane(rindex))->getComponentName(color), getName(z), raster->isInterleaved(), ncolors);
            #endif
            
            if (iop->aborted()) 
            {
                lock.unlock();
                return;            
            }
            float       *dest   = row.writable(z);
            const float *pixels = (const float*)raster->getPixels();
            for (int j = 0; j < width(); j++)
            {
                dest[j] = pixels[j*ncolors+(Y*width()*ncolors)+color];
            }
        }
    } 
    else
    {
        iop->error("FATAL! At this time, rat and images should be allocated!");
    }
    lock.unlock();
}

void 
ratReader::scanline_engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{ 
    lock.lock();
    IMG_Stat &stat = rat->getStat();
    buffer = rat->allocScanlineBuffer();
    float *scanline = (float *)buffer;

    int Y = height() - y - 1;
    row.range(0, width());
    #if defined(DEBUG)
    iop->warning("Scanline engine active.");
    #endif

    std::map<std::string, Channel> usedChans;
    std::map<Channel, Channel> toCopy;

    foreach(z, channels)
    {
        if (rat_chan_index.count(z) == 0)
        {
            iop->error("%s can't be found in rat_chan_index", getName(z));
            continue;
        }
        
        if (usedChans.find(channel_map[z]) != usedChans.end()) 
        {
            #if defined(DEBUG)
            iop->warning("%s is already used", getName(z));
            #endif
            toCopy[z] = usedChans[channel_map[z]];
            continue;
        }
        usedChans[channel_map[z]] = z;

        const int rindex = rat_chan_index[z].first;
        const int color  = rat_chan_index[z].second;
        IMG_Plane *plane = stat.getPlane(rindex);
        const int ncolors= IMGvectorSize(plane->getColorModel());

        #if defined(DEBUG)
        iop->warning("%s.%s writes to: %s", plane->getName(), plane->getComponentName(color), getName(z));
        #endif
  
        if (iop->aborted()) 
        {
            lock.unlock();
            return;            
        }

        float* dest = row.writable(z);  
        rat->readIntoBuffer(Y, scanline, plane);
        for (int j =0; j < width(); j++)
            dest[j]  = scanline[j*ncolors + color]; 
    }

    foreach (z, channels)
    {
        if (toCopy.find(z) != toCopy.end())
        {
            #if defined(DEBUG)
            iop->warning("%s is copied", getName(z));
            #endif
            
            float* dest      = row.writable(z);
            const float* src = row[toCopy[z]];
            for (int col = 0; col < width(); col++)
            {
                dest[col] = src[col];
            }
        }
    } 
    lock.unlock();
}

ratReader::~ratReader() 
{
    if (rat)
    {
        rat->close();
        delete rat;
        rat = NULL; 
        #if defined(DEBUG)
        iop->warning("rat deleted...");
        #endif
    }

    if (parms)
        delete parms;

    if (loaded)
    {
        for (int i=0; i<images.entries(); i++)
            delete images(i);
        #if defined(DEBUG)
        iop->warning("images deleted...");
        #endif
        loaded = false;
    }
        
    if (buffer)
    {
        #if defined(DEBUG)
        iop->warning("Deleting buffer");
        #endif
        delete (float*) buffer;
        buffer = NULL;
    }
}
