// NDK:
#include "DDImage/Reader.h"
#include "DDImage/Row.h"
#include "DDImage/ARRAY.h"
#include "DDImage/Thread.h"
//HDK:
#include "IMG/IMG_File.h"
#include "IMG/IMG_Stat.h"
#include "IMG/IMG_FileParms.h"
//#include "IMG/IMG_Format.h"
//#include "UT/UT_PtrArray.h"
//#include "PXL/PXL_Raster.h"
#include "IMG/IMG_FileTypes.h"
//#include "UT/UT_ValArray.h"

using namespace DD::Image;

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
    void open();
    void lookupChannels(std::set<Channel>& channel, const char* name);
    static const Reader::Description d;

private:
    IMG_File *rat;
    IMG_FileParms *parms;
    void *buffer;
    int depth, xres, yres;
    MetaData::Bundle _meta;
    std::map<Channel, const char*> channel_map;
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
    parms = new IMG_FileParms();
    // TODO: Add knob to control vertical orientation
    parms->orientImage(IMG_ORIENT_LEFT_FIRST, IMG_ORIENT_TOP_FIRST);
    parms->setDataType(IMG_FLOAT);
    parms->setInterleaved(IMG_NON_INTERLEAVED);
    
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
            Channel channel = *it;
            channel_map[channel] = chan_name.c_str();
            #if defined(DEBUG)
            iop->warning("Name Channel: %s", chan_name.c_str());
            iop->warning("Real Channel: %s", getName(channel));
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
ratReader::open() {}

void 
ratReader::engine(int y, int x, int xr, ChannelMask channels, Row& row) 
{ 
    // Lock and allocate buffer:
    lock.lock();
    buffer = rat->allocScanlineBuffer();
    float *scanline = (float *)buffer;

    // Pointers to Nuke's channels:
    int Y = height() - y - 1;
    row.range(0, width());
    
    std::map<std::string, Channel> usedChans;
    std::map<Channel, Channel> toCopy;

    foreach (z, channels)
    {
        if (usedChans.find(channel_map[z]) != usedChans.end())
        {
            toCopy[z] = usedChans[channel_map[z]];
            continue;
        }
        usedChans[channel_map[z]] = z;
    }


    // Main scanline loop:
    for (int i = 0; i < Y; i++)
    {
        rat->readIntoBuffer(i, scanline, 0);
        int nchan = 0;
        foreach(z, channels)
        {
            #if defined(DEBUG)
            iop->warning("Write to %s", getName(z));
            #endif
            float* dest = row.writable(z);
            for (int j =0; j < width(); j++)
            {
                dest[j]  = scanline[j+width()*nchan];
            }
            nchan++;
        }
    } 
    lock.unlock();
}

ratReader::~ratReader() 
{
    rat->close();
    if (rat)
        delete rat; 
    if (parms)
        delete parms;
    if (buffer)
    {
    #if defined(DEBUG)
        iop->warning("Deleting buffer");
    #endif
        delete (float*) buffer;
    }
}
