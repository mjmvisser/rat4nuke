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
    bool _discrete;
    bool _composite;

public:
    ratDeepReaderFormat()
    {
        _raw      = false;
        _premult  = false;
        _discrete = false;
        _composite= false;
    }

    bool getRaw() const
    {
        return _raw;
    }

    bool getPremult() const
    {
        return _premult;
    }

    bool getDiscrete() const
    {
        return _discrete;
    }
    
    bool getComposite() const
    {
        return _composite;
    }

    void knobs(Knob_Callback f)
    {
        Bool_knob(f, &_raw, "raw", "raw values");
        Tooltip(f, "Read the samples exactly 'as is' without processing.");
        Bool_knob(f, &_premult, "premult", "premultiply");
        Tooltip(f, "Premultiply values from file (if off, then it assumes they are already premultiplied)");
        Bool_knob(f, &_discrete, "discrete", "discrete");
        Tooltip(f, "Treat the file as discrete samples with the front and back being the same.  This is only relevant for \
        deep shadow files, color deep composting files are always discrete.");
        Bool_knob(f, &_composite, "composite", "composite");
        Tooltip(f, " Composited pixels have the data accumulated over from front to back. Uncomposited pixels will have the raw data for each z-record.");
        
        
    }

    void append(Hash& hash)
    {
        hash.append(_raw);
        hash.append(_premult);
        hash.append(_discrete);
        hash.append(_composite);
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
    const IMG_DeepShadowChannel *op;
    IMG_DeepPixelReader pixel(*rat);

     #if defined(DEBUG)
      _op->warning("Pixel Reader created.");
     #endif

    int height = yres;
    plane = DeepOutputPlane(channels, box);

    DD::Image::Knob* rawKnob       = _op->knob("raw");
    DD::Image::Knob* discreteKnob  = _op->knob("discrete");
    DD::Image::Knob* premultKnob   = _op->knob("premult");
    DD::Image::Knob* compositeKnob = _op->knob("composite");
    bool raw       = rawKnob       ? rawKnob->get_value() : false;
    bool discrete  = discreteKnob  ? discreteKnob->get_value() : false;
    bool premult   = premultKnob   ? premultKnob->get_value() : false;
    bool composite = compositeKnob ? compositeKnob->get_value() : false;

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
    op = NULL;

    for (int i = 0; i < nchannels; i++)
    {
        if (strcmp(rat->getChannel(i)->getName(), "C") == 0)
            cp = rat->getChannel(i);
        else if (strcmp(rat->getChannel(i)->getName(), "Pz") == 0)
            zp = rat->getChannel(i);
        else if (strcmp(rat->getChannel(i)->getName(), "Of") == 0)
            op = rat->getChannel(i);

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

    for (Box::iterator it = box.begin(); it != box.end(); it++)
    {
        float x = it.x;
        float y = it.y;
        outputContext.from_proxy_xy(x, y);
        // reverse scanlines:
        //y = height - y - 1;

        pixel.open(x, y);

        if (!composite)
            pixel.uncomposite(*zp, *op, false);

        int numpts = pixel.getDepth();

        DeepOutPixel pels(numpts * 6);// FIXME: channels.size()  

        const float *color;
        const float *color2;
        const float *zdepth;
        const float *zdepth2;
        const float *Of;

        for (int i = 0; i < numpts; i++)
        {
            color  = pixel.getData(*cp, i);
            zdepth = pixel.getData(*zp, i);
            Of     = pixel.getData(*op, i); 
            float alpha = 1;

            if (0) //TODO: always raw
            {
                if (i == numpts-1)
                    continue;

                color2  = pixel.getData(*cp, i+1);
                zdepth2 = pixel.getData(*zp, i+1);
                alpha   = (color[3] - color2[3]) / color[3];

                if (color[3] == color2[3])
                    continue;

                if (discrete)
                    zdepth2 = zdepth;
            }
            else
            {
                zdepth2 = zdepth;
                alpha   = color[3];
            }
                
            foreach(chan, channels)
            {
                if (chan == Chan_Z)
                    pels.push_back(1/zdepth[0]);
                else if (chan == Chan_DeepFront )
                    pels.push_back(zdepth[0]);
                else if ( chan == Chan_DeepBack )
                    pels.push_back(zdepth2[0]);
                else if (chan == Chan_Alpha)
                    pels.push_back(alpha);
                else if (channel_map.count(chan))
                {
                    if (premult && IsRGB(chan) && channel_map.count(Chan_Alpha))
                        pels.push_back(color[channel_map[chan]] * alpha);
                    else 
                        pels.push_back(color[channel_map[chan]]);
                }
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
