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
    static const Reader::Description d;

private:
    IMG_File *rat;
    IMG_FileParms *parms;
    void *buffer;
    int depth, xres, yres;
    MetaData::Bundle _meta;
    Lock lock;
};

static bool 
test(int fd, const unsigned char* block, int length)
{
    //return true; // Need to hard code this in case other parts of the code will fail.
    return block[0] == 'f' && block[1] == 'b' && block[2] == 't' && block[3] == 'H';
}

static 
Reader* build(Read* iop, int fd, const unsigned char* b, int n)
{
    return new ratReader(iop, fd);
}

const 
Reader::Description ratReader::d("rat\0", build, test);

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

    // Since RAT can store varying bitdepth per plane, so pixel-byte-algebra doesn't 
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

    // Set info:
    #if defined(DEBUG)
    iop->warning("Channel number: %i", depth);
    #endif
    set_info(stat.getXres(), stat.getYres(), depth);
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
    float* R = row.writable(Chan_Red);
    float* G = row.writable(Chan_Green);
    float* B = row.writable(Chan_Blue);
    float* A = row.writable(Chan_Alpha);
    
    // Main scanline loop:
    for (int i = 0; i < Y; i++)
    {
        rat->readIntoBuffer(i, scanline, 0);
        for (int j =0; j < width(); j++)
        {
            R[j]   = scanline[j];
            G[j]   = scanline[j+width()];
            B[j]   = scanline[j+width()*2];
            A[j]   = scanline[j+width()*3];
            // interleaved vesion:
            //R[j] = scanline[(j*4)+x];
            //G[j] = scanline[(j*4)+1+x];
            //B[j] = scanline[(j*4)+2+x];
            //A[j] = scanline[(j*4)+3+x];
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
