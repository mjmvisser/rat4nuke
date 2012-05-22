// NDK:
#include "DDImage/DeepReader.h"
#include "DDImage/DeepPlane.h"
#include "DDImage/Knobs.h"
#include "DDImage/Thread.h"
// HDK:
#include "IMG/IMG_DeepShadow.h"
#include "UT/UT_Options.h"
#include "UT/UT_Matrix4.h"
#include "UT/UT_Vector2.h"

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
        _discrete = true;
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
    IMG_DeepShadow *rat;
    int  xres, yres, nchannels;

    DD::Image::OutputContext outputContext;
    DD::Image::Knob* rawKnob;
    DD::Image::Knob* discreteKnob;
    DD::Image::Knob* premultKnob;
    DD::Image::Knob* compositeKnob;
    static Lock sLibraryLock;
    Lock lock;

    bool raw;
    bool discrete;
    bool premult;
    bool composite;

    std::map<Channel, const char*>          channel_map;
    std::map<Channel, std::pair<int, int> > rat_chan_index;


public:
void
lookupChannels(std::set<Channel>& channel, const char* name)
{
   
         if (strcmp(name, "C.red") == 0)
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
ratDeepReader(DeepReaderOwner* op, const std::string& filename) : DeepReader(op)
{
    rat           = NULL;
    rawKnob       = _op->knob("raw");
    discreteKnob  = _op->knob("discrete");
    premultKnob   = _op->knob("premult");
    compositeKnob = _op->knob("composite");
  
    if (!filename.length())
        return;

    outputContext = _owner->readerOutputContext();

    rat = new IMG_DeepShadow();
    if(!rat->open(filename.c_str()))
        return;

    #if defined(DEBUG)
    _op->warning("RAT opened");
    #endif

    for (int i = 0; i < rat->getChannelCount(); i++)
    {
        const IMG_DeepShadowChannel *chp;
        chp = rat->getChannel(i);
        nchannels += chp->getTupleSize();

        #if defined(DEBUG)
        _op->warning("Channel name: %s, size: %i", chp->getName(), chp->getTupleSize());
        #endif
    } 

    ChannelSet mask;
    for (int i = 0; i < rat->getChannelCount(); i++)
    {
        const IMG_DeepShadowChannel *chp;
        chp = rat->getChannel(i);
        const int nchan = chp->getTupleSize();

        for (int j = 0; j < nchan; j++)
        {
            std::string chan = "";
            if      (j == 0) chan = "red"; 
            else if (j == 1) chan = "green";
            else if (j == 2) chan = "blue"; 
            else if (j == 3) chan = "alpha";

            std::string chan_name = std::string(chp->getName()) + std::string(".") + chan;
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
                    _op->warning("Rat %s (%i,%i) becomes %s", chan_name.c_str(), i, j, getName(channel));
                    #endif
                    mask += channel;
                }
            }
            else
                _op->warning("Can't create a channel from %s", chan_name.c_str());
        }
    }

    mask += Mask_Deep;
    rat->resolution(xres, yres);
    setInfo(xres, yres, outputContext, mask);

    // Metadatas:
    const UT_Options *opt = rat->getTBFOptions();
    for (int i =0; i < opt->getNumOptions(); i++)
    {
        const char *name = opt->getOptionName(i);
        const UT_OptionType type = opt->getOptionType(i);

        #if defined(DEBUG)
        UT_String buff;
        opt->getOptionString(name, buff);
        printf("Option %s: value: %s\n", name, buff.buffer());
        #endif

        if (type == UT_OPTION_MATRIX4)
        {
            const UT_Matrix4D mat = opt->getOptionM4(name);
            _metaData.setData(name, mat.data(), 16);
        }
        else if (type == UT_OPTION_STRING)
        {
            const std::string str = opt->getOptionS(name);
            _metaData.setData(name, str);
        }
        else if (type == UT_OPTION_FPREAL)
        {
            const double d = opt->getOptionF(name);
            _metaData.setData(name, d);
        }
        else if (type == UT_OPTION_VECTOR2)
        {
            const UT_Vector2D d = opt->getOptionV2(name);
            _metaData.setData(name, d.data(), 2);
        }
        else if (type == UT_OPTION_VECTOR3)
        {
            const UT_Vector3D d = opt->getOptionV3(name);
            _metaData.setData(name, d.data(), 3);
        }
    }

    #if defined(HOUDINI_12)
    UT_Matrix4 mat;
    if (rat->getWorldToNDC(mat, true))
        _metaData.setData("space:worldtoNDC", mat.data(), 16);
    if (rat->getCameraToNDC(mat, true))
        _metaData.setData("space:cameratoNDC", mat.data(), 16);
    #endif
}

~ratDeepReader()
{
    if (rat) 
    {
        rat->close();
        delete rat;
        rat = NULL;
            
        #if defined(DEBUG)
        _op->warning("RAT deleted");
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

    raw       = rawKnob ? rawKnob->get_value() : false;
    discrete  = discreteKnob ? discreteKnob->get_value() : true;
    premult   = premultKnob ? premultKnob->get_value() : false;
    composite = compositeKnob ? compositeKnob->get_value() : false;
 
    const IMG_DeepShadowChannel *chp;
    const IMG_DeepShadowChannel *Pzp;
    const IMG_DeepShadowChannel *Ofp;
    int   Of_index = 0;
    Pzp = NULL; 
    Ofp = NULL;

    IMG_DeepPixelReader pixel(*rat);

     #if defined(DEBUG)
      _op->warning("Pixel Reader created.");
     #endif

    int height = yres;
    plane = DeepOutputPlane(channels, box);

     #if defined(DEBUG)
    _op->warning("Plane created.");
    #endif

    for (int i = 0; i < rat->getChannelCount(); i++)
    {
        if (strcmp(rat->getChannel(i)->getName(), "Pz") == 0)
            Pzp = rat->getChannel(i);
        else if (strcmp(rat->getChannel(i)->getName(), "Of") == 0)
        {
            Ofp = rat->getChannel(i);
            Of_index = i;
        }
    }

    if (!Pzp || !Ofp)
            _op->error("Can't find Of or Pz channels! Is it really a DCM file?");
    #if defined(DEBUG)
    else
        _op->warning("Of and Pz have been found.");
    #endif

    for (Box::iterator it = box.begin(); it != box.end(); it++)
    {
        float x = it.x;
        float y = it.y;
        outputContext.from_proxy_xy(x, y);

        pixel.open(x, y);

        if (!composite)
            pixel.uncomposite(*Pzp, *Ofp, false);

        int numpts = pixel.getDepth();
        DeepOutPixel pels(numpts * channels.size()); 
    
        for (int i = 0; i < numpts; i++)
        {
            const float *Pz;
            const float *Pz2;
            const float *Cf;

            foreach(chan, channels)
            {
                int rindex  = rat_chan_index[chan].first;
                int cindex  = rat_chan_index[chan].second;
                chp         = rat->getChannel(rindex);

                if (chan == Chan_Z || chan == Chan_DeepFront || chan == Chan_DeepBack)
                {
                    Pz = Pz2 = pixel.getData(*Pzp, i);
                    if (!discrete)
                    {
                        if (i+1 <= numpts)
                            Pz2 = pixel.getData(*Pzp, i+1);
                        else
                            continue;
                    }
                    if (chan == Chan_Z)
                        pels.push_back(1/Pz[0]);
                    else if (chan == Chan_DeepFront )
                        pels.push_back(Pz[0]);
                    else if ( chan == Chan_DeepBack )
                        pels.push_back(Pz2[0]);
                }
                else if (channel_map.count(chan))
                {
                    Cf = pixel.getData(*chp, i);
                    if (premult && IsRGB(chan) && channel_map.count(Chan_Alpha))
                        pels.push_back(Cf[cindex] * Cf[3]);
                    else 
                        pels.push_back(Cf[cindex]);
                }
                // FIXME: This is wrong, it should be push_back(0), 
                // but the only channels ending up here are Of.*, so this is fallback
                // mostly for deep shadow maps, which otherwise won't have Of.
                // Why 'Of' in deep shadow maps aren't peaked up by channel_map.count(chan)?
                else 
                {
                    Cf = pixel.getData(*Ofp, i);
                    pels.push_back(Cf[cindex]);
                    //pels.push_back(0); 
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
