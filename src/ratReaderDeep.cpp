// NDK:
#include "DDImage/DeepReader.h"
#include "DDImage/DeepPlane.h"
#include "DDImage/Knobs.h"
#include "DDImage/Thread.h"
// HDK:
#include "IMG/IMG_DeepShadow.h"

using namespace DD::Image;

namespace Nuke {
  namespace Deep {

static inline bool IsRGB(Channel channel)
{
  return channel == Chan_Red || channel == Chan_Blue || channel == Chan_Green;
}

static inline bool IsRGBA(Channel channel)
{
  return channel == Chan_Red || channel == Chan_Blue || channel == Chan_Green || channel == Chan_Alpha;
}


class ratDeepReaderFormat : public DeepReaderFormat
{
  friend class ratDeepReader;
  bool _raw;
  bool _premult;

public:
  ratDeepReaderFormat()
  {
    _raw = false;
    _premult = false;
  }

  bool getRaw() const
  {
    return _raw;
  }

  bool getPremult() const
  {
    return _premult;
  }

  void knobs(Knob_Callback f)
  {
    Bool_knob(f, &_raw, "raw", "raw values");

    Bool_knob(f, &_premult, "premult", "premultiply");
    Tooltip(f, "Premultiply values from file (if off, then it assumes they are already premultiplied)");
  }

  void append(Hash& hash)
  {
    hash.append(_raw);
    hash.append(_premult);
  }
};

class ratDeepReader : public DeepReader
{
    static Lock sLibraryLock;
    IMG_DeepShadow *rat;
    Lock lock;
    int  xres, yres, nchannels;
    DD::Image::OutputContext outputContext;

public:
ratDeepReader(DeepReaderOwner* op, const std::string& filename) : DeepReader(op)
{
    rat = NULL;
    if (!filename.length())
        return;
    outputContext = _owner->readerOutputContext();

    rat = new IMG_DeepShadow();
    if(!rat->open(filename.c_str()))
        return;

    #if defined(DEBUG)
    _op->warning("rat opened");
    #endif

    rat->resolution(xres, yres);
    nchannels = rat->getChannelCount();

    setInfo(xres, yres, outputContext, Mask_RGB | Mask_Alpha | Mask_Deep);

    //_metaData.setData("dtex/np", NP, 16);
    //_metaData.setData("dtex/nl", Nl, 16);
}

~ratDeepReader()
{
    if (rat) 
    {
        rat->close();
        rat = NULL;
            
        #if defined(DEBUG)
        _op->warning("rat deleted");
        #endif
    }
}

void 
open(const std::string& filename){}

void 
doDeepEngine(DD::Image::Box box, const ChannelSet& channels, DeepOutputPlane& plane)
{
    Guard g(lock);
    if (!rat)
        _op->error("error opening file");
 
    const IMG_DeepShadowChannel *cp;
    const IMG_DeepShadowChannel *zp;
    IMG_DeepPixelReader pixel(*rat);

     #if defined(DEBUG)
      _op->warning("Pixel Reader created.");
     #endif

    int height = yres;
    plane = DeepOutputPlane(channels, box);

    // TODO: RGBA for now only:
    std::map<Channel, int> channel_map;
    if (nchannels) // if (nchannels == 4)
    {
        channel_map[Chan_Red] = 0;
        channel_map[Chan_Green] = 1;
        channel_map[Chan_Blue] = 2;
        channel_map[Chan_Alpha] = 3;
        //channel_map[Chan_Z] = 4;
        //raw = true;
    } 
    else 
        _op->error("Can't find any channels");// chanMap[Chan_Alpha] = 0;

    zp = NULL; 
    cp = NULL;

    for (int i = 0; i < nchannels; i++)
    {
        if (strcmp(rat->getChannel(i)->getName(), "C") == 0)
            cp = rat->getChannel(i);
        else if (strcmp(rat->getChannel(i)->getName(), "Pz") == 0)
            zp = rat->getChannel(i);

        #if defined(DEBUG)
        if (cp && zp)
            _op->warning("Found channels: %s, %s", cp->getName(), zp->getName());
        #endif
    }

    #if defined(DEBUG)
    _op->warning("channels.size(): %i", channels.size());
    printf("Channels: ");
    foreach(chan, channels)   
        printf("%s, ", getName(chan));
    printf("\n");
    #endif

     std::vector<float> pts (4); // FIXME: nchannels
     //std::vector<float> pts2(nchannels); // FIXME: composited output (not raw).

    for (Box::iterator it = box.begin(); it != box.end(); it++)
    {
        float x = it.x;
        float y = it.y;
        outputContext.from_proxy_xy(x, y);
        
        pixel.open(x, height - 1 - y);
        int numpts = pixel.getDepth();

        DeepOutPixel pels(numpts * 6);// FIXME: channels.size()  

        const float *color;
        const float *zdepth;
        for (int i = 0; i < numpts; i++)
        {
            color = pixel.getData(*cp, i);
            zdepth = pixel.getData(*zp, i);

            foreach(chan, channels)
            {
                if (chan == Chan_Z)
                    pels.push_back(1/zdepth[0]);
                else if (chan == Chan_DeepFront )
                    pels.push_back(zdepth[0]);
                else if ( chan == Chan_DeepBack )
                    pels.push_back(zdepth[0]);
                else if (chan == Chan_Alpha)
                    pels.push_back(color[3]);
                else if (channel_map.count(chan))
                    pels.push_back( color[channel_map[chan]]);
            }
        }
        plane.addPixel(pels);
    }
    pixel.close();
}
};

static DeepReader* build(DeepReaderOwner* op, const std::string& fn)
{
  return new ratDeepReader(op, fn);
}

static DeepReaderFormat* buildFormat(DeepReaderOwner* op)
{
  return new ratDeepReaderFormat();
}

static const DeepReader::Description d("rat\0dcm\0dsm\0", "rat", build, buildFormat);

Lock ratDeepReader::sLibraryLock;
    } // End of namespace Deep
} // End of namespace Nuke
