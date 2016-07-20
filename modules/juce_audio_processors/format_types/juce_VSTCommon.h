/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2015 - ROLI Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

#ifndef JUCE_VSTCOMMON_H_INCLUDED
#define JUCE_VSTCOMMON_H_INCLUDED

//==============================================================================
struct SpeakerMappings  : private AudioChannelSet // (inheritance only to give easier access to items in the namespace)
{
    struct Mapping
    {
        VstInt32 vst2;
        ChannelType channels[13];

        bool matches (const Array<ChannelType>& chans) const noexcept
        {
            const int n = sizeof (channels) / sizeof (ChannelType);

            for (int i = 0; i < n; ++i)
            {
                if (channels[i] == unknown)  return (i == chans.size());
                if (i == chans.size())       return (channels[i] == unknown);

                if (channels[i] != chans.getUnchecked(i))
                    return false;
            }

            return true;
        }
    };

    static AudioChannelSet vstArrangementTypeToChannelSet (VstInt32 arr, int fallbackNumChannels)
    {
        for (const Mapping* m = getMappings(); m->vst2 != kSpeakerArrEmpty; ++m)
        {
            if (m->vst2 == arr)
            {
                AudioChannelSet s;

                for (int i = 0; m->channels[i] != 0; ++i)
                    s.addChannel (m->channels[i]);

                return s;
            }
        }

        return AudioChannelSet::discreteChannels (fallbackNumChannels);
    }

    static AudioChannelSet vstArrangementTypeToChannelSet (const VstSpeakerArrangement& arr)
    {
        return vstArrangementTypeToChannelSet (arr.type, arr.numChannels);
    }

    static VstInt32 channelSetToVstArrangementType (AudioChannelSet channels)
    {
        Array<AudioChannelSet::ChannelType> chans (channels.getChannelTypes());

        if (channels == AudioChannelSet::disabled())
            return kSpeakerArrEmpty;

        for (const Mapping* m = getMappings(); m->vst2 != kSpeakerArrEmpty; ++m)
            if (m->matches (chans))
                return m->vst2;

        return kSpeakerArrUserDefined;
    }

    static void channelSetToVstArrangement (const AudioChannelSet& channels, VstSpeakerArrangement& result)
    {
        result.type = channelSetToVstArrangementType (channels);
        result.numChannels = channels.size();

        for (int i = 0; i < result.numChannels; ++i)
        {
            VstSpeakerProperties& speaker = result.speakers[i];

            zeromem (&speaker, sizeof (VstSpeakerProperties));
            speaker.type = getSpeakerType (channels.getTypeOfChannel (i));
        }
    }

    static const Mapping* getMappings() noexcept
    {
        static const Mapping mappings[] =
        {
            { kSpeakerArrMono,           { centre, unknown } },
            { kSpeakerArrStereo,         { left, right, unknown } },
            { kSpeakerArrStereoSurround, { leftSurround, rightSurround, unknown } },
            { kSpeakerArrStereoCenter,   { leftCentre, rightCentre, unknown } },
            { kSpeakerArrStereoSide,     { leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArrStereoCLfe,     { centre, subbass, unknown } },
            { kSpeakerArr30Cine,         { left, right, centre, unknown } },
            { kSpeakerArr30Music,        { left, right, surround, unknown } },
            { kSpeakerArr31Cine,         { left, right, centre, subbass, unknown } },
            { kSpeakerArr31Music,        { left, right, subbass, surround, unknown } },
            { kSpeakerArr40Cine,         { left, right, centre, surround, unknown } },
            { kSpeakerArr40Music,        { left, right, leftSurround, rightSurround, unknown } },
            { kSpeakerArr41Cine,         { left, right, centre, subbass, surround, unknown } },
            { kSpeakerArr41Music,        { left, right, subbass, leftSurround, rightSurround, unknown } },
            { kSpeakerArr50,             { left, right, centre, leftSurround, rightSurround, unknown } },
            { kSpeakerArr51,             { left, right, centre, subbass, leftSurround, rightSurround, unknown } },
            { kSpeakerArr60Cine,         { left, right, centre, leftSurround, rightSurround, surround, unknown } },
            { kSpeakerArr60Music,        { left, right, leftSurround, rightSurround, leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArr61Cine,         { left, right, centre, subbass, leftSurround, rightSurround, surround, unknown } },
            { kSpeakerArr61Music,        { left, right, subbass, leftSurround, rightSurround, leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArr70Cine,         { left, right, centre, leftSurround, rightSurround, topFrontLeft, topFrontRight, unknown } },
            { kSpeakerArr70Music,        { left, right, centre, leftSurround, rightSurround, leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArr71Cine,         { left, right, centre, subbass, leftSurround, rightSurround, topFrontLeft, topFrontRight, unknown } },
            { kSpeakerArr71Music,        { left, right, centre, subbass, leftSurround, rightSurround, leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArr80Cine,         { left, right, centre, leftSurround, rightSurround, topFrontLeft, topFrontRight, surround, unknown } },
            { kSpeakerArr80Music,        { left, right, centre, leftSurround, rightSurround, surround, leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArr81Cine,         { left, right, centre, subbass, leftSurround, rightSurround, topFrontLeft, topFrontRight, surround, unknown } },
            { kSpeakerArr81Music,        { left, right, centre, subbass, leftSurround, rightSurround, surround, leftRearSurround, rightRearSurround, unknown } },
            { kSpeakerArr102,            { left, right, centre, subbass, leftSurround, rightSurround, topFrontLeft, topFrontCentre, topFrontRight, topRearLeft, topRearRight, subbass2, unknown } },
            { kSpeakerArrEmpty,          { unknown } }
        };

        return mappings;
    }

    static inline VstInt32 getSpeakerType (AudioChannelSet::ChannelType type) noexcept
    {
        switch (type)
        {
            case AudioChannelSet::left:              return kSpeakerL;
            case AudioChannelSet::right:             return kSpeakerR;
            case AudioChannelSet::centre:            return kSpeakerC;
            case AudioChannelSet::subbass:           return kSpeakerLfe;
            case AudioChannelSet::leftSurround:      return kSpeakerLs;
            case AudioChannelSet::rightSurround:     return kSpeakerRs;
            case AudioChannelSet::leftCentre:        return kSpeakerLc;
            case AudioChannelSet::rightCentre:       return kSpeakerRc;
            case AudioChannelSet::surround:          return kSpeakerS;
            case AudioChannelSet::leftRearSurround:  return kSpeakerSl;
            case AudioChannelSet::rightRearSurround: return kSpeakerSr;
            case AudioChannelSet::topMiddle:         return kSpeakerTm;
            case AudioChannelSet::topFrontLeft:      return kSpeakerTfl;
            case AudioChannelSet::topFrontCentre:    return kSpeakerTfc;
            case AudioChannelSet::topFrontRight:     return kSpeakerTfr;
            case AudioChannelSet::topRearLeft:       return kSpeakerTrl;
            case AudioChannelSet::topRearCentre:     return kSpeakerTrc;
            case AudioChannelSet::topRearRight:      return kSpeakerTrr;
            case AudioChannelSet::subbass2:          return kSpeakerLfe2;
            default: break;
        }

        return 0;
    }

    static inline AudioChannelSet::ChannelType getChannelType (VstInt32 type) noexcept
    {
        switch (type)
        {
            case kSpeakerL:     return AudioChannelSet::left;
            case kSpeakerR:     return AudioChannelSet::right;
            case kSpeakerC:     return AudioChannelSet::centre;
            case kSpeakerLfe:   return AudioChannelSet::subbass;
            case kSpeakerLs:    return AudioChannelSet::leftSurround;
            case kSpeakerRs:    return AudioChannelSet::rightSurround;
            case kSpeakerLc:    return AudioChannelSet::leftCentre;
            case kSpeakerRc:    return AudioChannelSet::rightCentre;
            case kSpeakerS:     return AudioChannelSet::surround;
            case kSpeakerSl:    return AudioChannelSet::leftRearSurround;
            case kSpeakerSr:    return AudioChannelSet::rightRearSurround;
            case kSpeakerTm:    return AudioChannelSet::topMiddle;
            case kSpeakerTfl:   return AudioChannelSet::topFrontLeft;
            case kSpeakerTfc:   return AudioChannelSet::topFrontCentre;
            case kSpeakerTfr:   return AudioChannelSet::topFrontRight;
            case kSpeakerTrl:   return AudioChannelSet::topRearLeft;
            case kSpeakerTrc:   return AudioChannelSet::topRearCentre;
            case kSpeakerTrr:   return AudioChannelSet::topRearRight;
            case kSpeakerLfe2:  return AudioChannelSet::subbass2;
            default: break;
        }

        return AudioChannelSet::unknown;
    }
};

#endif   // JUCE_VSTCOMMON_H_INCLUDED
