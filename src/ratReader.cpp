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
        _use_scanline_engine = false;
        _reverse_scanlines   = true;
    }
    void knobs(Knob_Callback c)
    {
        Bool_knob(c, &_use_scanline_engine, "use_scanline_engine", "use scanline engine");
        Tooltip(c, "Use either scanline or full raster engine to read data from *.rat files."
                "Full raster should perform better for the expanse of memory bigger allocation.");
        Bool_knob(c, &_reverse_scanlines, "reverse_scanlines", "reverse scanlines");
        Tooltip(c, "Unlike Nuke images, *.rat files could be buttom-up. Use this option to reverse it.");
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
    void engine(int y, int x, int r, ChannelMask, Row &);
    void scanline_engine(int y, int x, int r, ChannelMask, Row &);
    void raster_engine(int y, int x, int r, ChannelMask, Row &);
    void open();
    void lookupChannels(std::set<Channel>& channel, const char* name);
    static const Reader::Description d;

private:
    IMG_File *rat;
    IMG_FileParms *parms;
    UT_PtrArray<PXL_Raster *> images;

    void *buffer;
    bool loaded;
    int depth, xres, yres;
    bool use_scanline_engine;
    bool reverse_scanlines;
    std::map<Channel, const char*> channel_map;
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
Reader::Description ratReader::d("rat\0", build, test);

// This is from NDK exrReader.cpp example
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
        channel.insert(Reader::channel(name));
}

ratReader::ratReader(Read *r, int fd): Reader(r)
{
    // Some read params have to be specified: 
    buffer = NULL;
    loaded = false;
    use_scanline_engine = false;
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

    // Set rest of settings:
    parms->setDataType(IMG_FLOAT);
    //parms->setInterleaved(IMG_NON_INTERLEAVED);
    
    // Create and open rat file, get stats:
    rat = IMG_File::open(r->filename(), parms);
    if (!rat)
    {
        iop->error("Failed to open .rat file.");
    }
    const IMG_Stat &stat = rat->getStat();
    depth = 0;

    // Since RAT can store varying bitdepth per plane, pixel-byte-algebra doesn't 
    // help in finding out a number of channels. We need to iterate over planes. 
    for (int i = 0; i < stat.getNumPlanes(); i++)
    {
        IMG_Plane *plane = stat.getPlane(i);
        // The easiest yet not unequivocal way to determine 2d versus deep RAT files:
        if (!strcmp(plane->getName(), "Depth-Complexity"))
            iop->error("Deep shadow/camera maps not supported.");
        #if defined(DEBUG)
        iop->warning("Plane name: %s", plane->getName());
        #endif
        depth += IMGvectorSize(plane->getColorModel()); 
    } 

    // For each channel in the file, create or locate the matching Nuke channel
    // number, and store it in the channel_map
    ChannelSet mask;
    std::set<Channel> channels;
    std::set<Channel>::iterator it;
    for (int i = 0; i < stat.getNumPlanes(); i++)
    {
        IMG_Plane *plane = stat.getPlane(i);
        int        nchan = IMGvectorSize(plane->getColorModel());

        for (int j = 0; j < nchan; j++)
        {
            std::string chan_name;
            std::string chan = std::string(plane->getComponentName(j) ? plane->getComponentName(j): "r"); 
            if      (chan == "r") chan = "red";
            else if (chan == "g") chan = "green";
            else if (chan == "b") chan = "blue";
            else if (chan == "a") chan = "alpha";
            chan_name = std::string(plane->getName()) + std::string(".") + chan;
            lookupChannels(channels, chan_name.c_str());
            it = channels.end();
            Channel channel     = *it;
            channel_map[channel]= chan_name.c_str();
            std::pair<int, int> idx(i, j);
            rat_chan_index[channel] = idx;
            #if defined(DEBUG)
            iop->warning("Rat component %s becomes %s", chan_name.c_str(), getName(channel));
            #endif
            mask += channel;
        }
    }
    // Set info:
    #if defined(DEBUG)
    iop->warning("Channel number: %i", depth);
    #endif
    set_info(stat.getXres(), stat.getYres(), depth);
    info_.channels(mask);
}

void
ratReader::open()
{
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

void 
ratReader::engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{
    if (use_scanline_engine)
        scanline_engine(y, x, xr, channels, row);
    else
        raster_engine(y, x, xr, channels, row);
}


void 
ratReader::raster_engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{ 
    // Lock and allocate buffer:
    lock.lock();
    const IMG_Stat &stat = rat->getStat();

    // Pointers to Nuke's channels:
    int Y = height() - y - 1;
    row.range(0, width());
    
    if (!loaded)
        this->open();
    else
    {
        foreach(z, channels)
        {
            int rindex = rat_chan_index[z].first;
            int color  = rat_chan_index[z].second;
            PXL_Raster *raster  = images(rindex);

            #if defined(DEBUG)
            //iop->warning("Write to %s", getName(z));
            iop->warning("%s.%s writes to: %s", (stat.getPlane(rindex))->getName(), \
                        (stat.getPlane(rindex))->getComponentName(color), getName(z));
            #endif

            float       *dest   = row.writable(z);
            const float *pixels = (const float*)raster->getPixels();
            // Scanline loop:
            for (int i = 0; i < Y; i++)
            {
                for (int j =0; j < width(); j++)
                    dest[j]  = pixels[j+(i*width())+color];
            }
        }
        
    } 
    lock.unlock();
}

void 
ratReader::scanline_engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{ 
    // Lock and allocate buffer:
    lock.lock();
    IMG_Stat &stat = rat->getStat();
    buffer = rat->allocScanlineBuffer();
    float *scanline = (float *)buffer;

    // Pointers to Nuke's channels:
    int Y = height() - y - 1;
    row.range(0, width());
    foreach(z, channels)
    {
        int rindex = rat_chan_index[z].first;
        int color  = rat_chan_index[z].second;
        IMG_Plane *plane = stat.getPlane(rindex);

        #if defined(DEBUG)
        iop->warning("%s.%s writes to: %s", plane->getName(), plane->getComponentName(color), getName(z));
        #endif

        float* dest = row.writable(z);
        // Scanline loop:
        for (int i = 0; i < Y; i++)
        {
            rat->readIntoBuffer(i, scanline, plane);
            for (int j =0; j < width(); j++)
                dest[j]  = scanline[j + (i*width()) + color];
        }
    } 
}

ratReader::~ratReader() 
{
    rat->close();
    if (rat)
        delete rat; 
    if (parms)
        delete parms;
    if (loaded)
    {
        for (int i=0; i<images.entries(); i++)
            delete images(i);
        #if defined(DEBUG)
        iop->warning("Deleting images");
        #endif
        loaded = false;
    }
        
    //if (buffer)
    //{
    #if defined(DEBUG)
    //    iop->warning("Deleting buffer");
    #endif
    //    delete (float*) buffer;
    //}
}
