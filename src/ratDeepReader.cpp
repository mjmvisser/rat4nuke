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
    if(!rat->open(filename.buffer());
        return;

    rat->resolution(xres, yres);
    rat->getChannelCount(nchannels);

    setInfo(xres, yres, outputContext, Mask_RGB | Mask_Alpha | Mask_Deep);

    //_metaData.setData("dtex/np", NP, 16);
    //_metaData.setData("dtex/nl", Nl, 16);
}

~dtexDeepReader()
{
    if (rat) 
    {
        rat->close();
        rat = NULL;
    }
}

void 
open(const std::string& filename){}

void 
doDeepEngine(DD::Image::Box box, const ChannelSet& channels, DeepOutputPlane& plane)
{
    Guard g(_engineLock);
    if (!rat)
        _op->error("error opening file");
 
    const IMG_DeepShadowChannel *chp;
    IMG_DeepPixelReader          pixel(&rat);

    //DtexImage* image;
    //DtexGetImageByIndex(_dtexFile, 0, &image); 
    
    int height = yres;
    //DtexPixel* pixel = DtexMakePixel(_numChans);
    plane = DeepOutputPlane(channels, box);

    // TODO: RGBA for now only:
    std::map<Channel, int> channel_map;
    if (nchannels) // if (nchannels == 4)
    {
        chanMap[Chan_Red] = 0;
        chanMap[Chan_Green] = 1;
        chanMap[Chan_Blue] = 2;
        chanMap[Chan_Alpha] = 3;
        raw = true;
    } 
    else 
        _op->error("Can't find any channels");// chanMap[Chan_Alpha] = 0;

    std::vector<float> pts (nchannels);
    std::vector<float> pts2(nchannels);

    for (Box::iterator it = box.begin(); it != box.end(); it++)
    {
        float x = it.x;
        float y = it.y;
        _outputContext.from_proxy_xy(x, y);
        
        pixel->open(x, height - 1 - y);
        int numpts = pixel->getDepth();

        DeepOutPixel pels(numpts * channels.size());

    }

      /*
      DtexGetPixel(image, int(x), height - 1 - int(y), pixel);

      int numpts = DtexPixelGetNumPoints(pixel);

      DeepOutPixel pels(numpts * channels.size());

      for (int j = 0; j < numpts; j ++ ) {
          
        float z;
        DtexPixelGetPoint(pixel, j, &z, &pts[0]);
        
        if (!raw) {
          float z2;
          DtexPixelGetPoint(pixel, j+1, &z2, &pts2[0]);

          if (pts[chanMap[Chan_Alpha]] == pts2[chanMap[Chan_Alpha]])
            continue;
        }
        
        float alpha = 1;

        if (chanMap.count(Chan_Alpha)) {
          const int alphaChan = chanMap[Chan_Alpha];
          
          alpha = raw ? 
            pts[alphaChan] :
            ((pts[alphaChan] - pts2[alphaChan]) / pts[alphaChan]);
        }

        foreach(chan, channels) {

          if (chan == Chan_Z)
            pels.push_back(1/z);
          else if (chan == Chan_DeepFront || chan == Chan_DeepBack)
            pels.push_back(z);
          else if (chan == Chan_Alpha)
            pels.push_back(alpha);
          else if (chanMap.count(chan)) {
            if (premult && IsRGB(chan) && chanMap.count(Chan_Alpha)) {
              pels.push_back(pts[chanMap[chan]] * alpha);
            }
            else
              pels.push_back(pts[chanMap[chan]]);              
          }
          else if (IsRGBA(chan))
            pels.push_back(alpha);
          else
            pels.push_back(0);
        }
      }

      plane.addPixel(pels);
    }
    
    DtexDestroyPixel(pixel);
  }*/
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


  }
}
