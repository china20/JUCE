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

#include "../JuceLibraryCode/JuceHeader.h"
#include "GraphEditorPanel.h"
#include "InternalFilters.h"
#include "MainHostWindow.h"


//==============================================================================
class PluginWindow;
static Array <PluginWindow*> activePluginWindows;

PluginWindow::PluginWindow (Component* const pluginEditor,
                            AudioProcessorGraph::Node* const o,
                            WindowFormatType t,
                            AudioProcessorGraph& audioGraph)
    : DocumentWindow (pluginEditor->getName(), Colours::lightblue,
                      DocumentWindow::minimiseButton | DocumentWindow::closeButton),
      graph (audioGraph),
      owner (o),
      type (t)
{
    setSize (400, 300);

    setContentOwned (pluginEditor, true);

    setTopLeftPosition (owner->properties.getWithDefault (getLastXProp (type), Random::getSystemRandom().nextInt (500)),
                        owner->properties.getWithDefault (getLastYProp (type), Random::getSystemRandom().nextInt (500)));

    owner->properties.set (getOpenProp (type), true);

    setVisible (true);

    activePluginWindows.add (this);
}

void PluginWindow::closeCurrentlyOpenWindowsFor (const uint32 nodeId)
{
    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner->nodeId == nodeId)
            delete activePluginWindows.getUnchecked (i);
}

void PluginWindow::closeAllCurrentlyOpenWindows()
{
    if (activePluginWindows.size() > 0)
    {
        for (int i = activePluginWindows.size(); --i >= 0;)
            delete activePluginWindows.getUnchecked (i);

        Component dummyModalComp;
        dummyModalComp.enterModalState();
        MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

//==============================================================================
class ProcessorProgramPropertyComp : public PropertyComponent,
                                     private AudioProcessorListener
{
public:
    ProcessorProgramPropertyComp (const String& name, AudioProcessor& p, int index_)
        : PropertyComponent (name),
          owner (p),
          index (index_)
    {
        owner.addListener (this);
    }

    ~ProcessorProgramPropertyComp()
    {
        owner.removeListener (this);
    }

    void refresh() { }
    void audioProcessorChanged (AudioProcessor*) { }
    void audioProcessorParameterChanged (AudioProcessor*, int, float) { }

private:
    AudioProcessor& owner;
    const int index;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessorProgramPropertyComp)
};

class ProgramAudioProcessorEditor : public AudioProcessorEditor
{
public:
    ProgramAudioProcessorEditor (AudioProcessor* const p)
        : AudioProcessorEditor (p)
    {
        jassert (p != nullptr);
        setOpaque (true);

        addAndMakeVisible (panel);

        Array<PropertyComponent*> programs;

        const int numPrograms = p->getNumPrograms();
        int totalHeight = 0;

        for (int i = 0; i < numPrograms; ++i)
        {
            String name (p->getProgramName (i).trim());

            if (name.isEmpty())
                name = "Unnamed";

            ProcessorProgramPropertyComp* const pc = new ProcessorProgramPropertyComp (name, *p, i);
            programs.add (pc);
            totalHeight += pc->getPreferredHeight();
        }

        panel.addProperties (programs);

        setSize (400, jlimit (25, 400, totalHeight));
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::grey);
    }

    void resized() override
    {
        panel.setBounds (getLocalBounds());
    }

private:
    PropertyPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgramAudioProcessorEditor)
};

class IOConfigurationAudioProcessorEditor : public AudioProcessorEditor, public Button::Listener
{
    struct InputOutputConfig;

public:
    IOConfigurationAudioProcessorEditor (AudioProcessor* const p)
        : AudioProcessorEditor (p),
          title ("title", p->getName()),
          applyButton ("Apply")
    {
        jassert (p != nullptr);
        setOpaque (true);

        title.setFont (title.getFont().withStyle (Font::bold));
        addAndMakeVisible (title);
        applyButton.addListener (this);

        if (p->getBusCount (true)  > 0 || p->canAddBus (true))
            addAndMakeVisible (inConfig = new InputOutputConfig (*this, true));

        if (p->getBusCount (false) > 0 || p->canAddBus (false))
            addAndMakeVisible (outConfig = new InputOutputConfig (*this, false));

        addAndMakeVisible (applyButton);

        currentLayout = p->getAudioBusesLayout();

        if (inConfig != nullptr)
            inConfig->updateBusConfig (currentLayout.inputBuses);

        if (outConfig != nullptr)
            outConfig->updateBusConfig (currentLayout.outputBuses);

        setSize (400, (inConfig != nullptr && outConfig != nullptr ? 160 : 0) + 250);
    };

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::white);
    }

    void resized() override
    {
        Rectangle<int> r = getLocalBounds().reduced (10);

        title.setBounds (r.removeFromTop (14));
        r.reduce (10, 0);

        if (inConfig != nullptr)
            inConfig->setBounds (r.removeFromTop (160));

        if (outConfig != nullptr)
            outConfig->setBounds (r.removeFromTop (160));

        applyButton.setBounds (r.removeFromRight (80));
    }

    void suspend()
    {
        if (AudioProcessorGraph* graph = getGraph())
        {
            graph->suspendProcessing (true);
            graph->releaseResources();
        }
    }

    void resume()
    {
        if (AudioProcessorGraph* graph = getGraph())
        {
            graph->prepareToPlay (graph->getSampleRate(), graph->getBlockSize());
            graph->suspendProcessing (false);

            if (GraphDocumentComponent* graphEditor = getGraphEditor())
                if (GraphEditorPanel* panel = graphEditor->graphPanel)
                    panel->updateComponents();
        }
    }

    void buttonClicked (Button*) override
    {
        bool wasSuccesful = true;

        if (AudioProcessor* p = getAudioProcessor())
        {
            if (currentLayout != p->getAudioBusesLayout())
            {
                suspend();
                wasSuccesful = p->setAudioBusesLayout (currentLayout);
                resume();
            }

            if (wasSuccesful)
                getTopLevelComponent()->userTriedToCloseWindow();
            else
                getLookAndFeel().playAlertSound();
        }
    }

    void updateConfig (const AudioChannelSet& set, bool isInput, int busIdx)
    {
        AudioProcessor::AudioBusesLayout newLayout = currentLayout;
        newLayout.getChannelSet (isInput, busIdx) = set;

        if (currentLayout != newLayout)
        {
            if (AudioProcessor* p = getAudioProcessor())
            {
                newLayout = p->getNextBestLayout (newLayout);
                currentLayout = newLayout;

                if (inConfig != nullptr)
                    inConfig->updateBusConfig (currentLayout.inputBuses);

                if (outConfig != nullptr)
                    outConfig->updateBusConfig (currentLayout.outputBuses);
            }
        }
    }

    void addBus (bool isInput)
    {
        if (AudioProcessor* p = getAudioProcessor())
        {
            suspend();
            bool wasSuccesful = p->addBus (isInput);

            if (wasSuccesful)
            {
                currentLayout = p->getAudioBusesLayout();

                if (inConfig != nullptr)
                    inConfig->updateBusConfig (currentLayout.inputBuses);

                if (outConfig != nullptr)
                    outConfig->updateBusConfig (currentLayout.outputBuses);

                if (inConfig != nullptr && isInput)
                    inConfig->updateSupported();

                if (outConfig != nullptr && ! isInput)
                    outConfig->updateSupported();
            }
            else
                LookAndFeel::getDefaultLookAndFeel().playAlertSound();

            resume();
        }
    }

    void removeBus (bool isInput)
    {
        if (AudioProcessor* p = getAudioProcessor())
        {
            suspend();
            bool wasSuccesful = p->removeBus (isInput);

            if (wasSuccesful)
            {
                currentLayout = p->getAudioBusesLayout();

                if (inConfig != nullptr)
                    inConfig->updateBusConfig (currentLayout.inputBuses);

                if (outConfig != nullptr)
                    outConfig->updateBusConfig (currentLayout.outputBuses);

                if (inConfig != nullptr && isInput)
                    inConfig->updateSupported();

                if (outConfig != nullptr && ! isInput)
                    outConfig->updateSupported();
            }

            resume();
        }
    }
private:
    MainHostWindow* getMainWindow() const
    {
       Component* comp;

        for (int idx = 0; (comp = Desktop::getInstance().getComponent(idx)) != nullptr; ++idx)
            if (MainHostWindow* mainWindow = dynamic_cast<MainHostWindow*> (comp))
                return mainWindow;

        return nullptr;
    }

    GraphDocumentComponent* getGraphEditor() const
    {
        if (MainHostWindow* mainWindow = getMainWindow())
        {
            if (GraphDocumentComponent* graphEditor = mainWindow->getGraphEditor())
                return graphEditor;
        }

        return nullptr;
    }

    AudioProcessorGraph* getGraph() const
    {
        if (GraphDocumentComponent* graphEditor = getGraphEditor())
            return &graphEditor->graph.getGraph();

        return nullptr;
    }

    //==============================================================================
    struct InputOutputConfig : Component, ComboBox::Listener, Button::Listener
    {
        InputOutputConfig (IOConfigurationAudioProcessorEditor& parent, bool direction)
            : owner (parent),
              ioTitle ("ioLabel", direction ? "Input Configuration" : "Output Configuration"),
              nameLabel ("nameLabel", "Bus Name:"),
              layoutLabel ("layoutLabel", "Channel Layout:"),
              enabledToggle ("Enabled"),
              ioBuses (*this, direction),
              isInput (direction),
              currentBus (-1)
        {
            updateSupported();

            ioTitle.setFont (ioTitle.getFont().withStyle (Font::bold));
            nameLabel.setFont (nameLabel.getFont().withStyle (Font::bold));
            layoutLabel.setFont (layoutLabel.getFont().withStyle (Font::bold));
            enabledToggle.setClickingTogglesState (true);

            layouts.addListener (this);
            enabledToggle.addListener (this);

            addAndMakeVisible (layoutLabel);
            addAndMakeVisible (layouts);
            addAndMakeVisible (enabledToggle);
            addAndMakeVisible (ioTitle);
            addAndMakeVisible (nameLabel);
            addAndMakeVisible (name);
            addAndMakeVisible (ioBuses);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Colours::white);
        }

        void resized() override
        {
            Rectangle<int> r = getLocalBounds().reduced (10);

            ioTitle.setBounds (r.removeFromTop (14));
            r.reduce (10, 0);
            r.removeFromTop (16);

            ioBuses.setBounds (r.removeFromTop (ioBuses.getHeight()));

            {
                Rectangle<int> label = r.removeFromTop (24);

                nameLabel.setBounds (label.removeFromLeft (100));
                enabledToggle.setBounds (label.removeFromRight (80));
                name.setBounds (label);
            }

            {
                Rectangle<int> label = r.removeFromTop (24);

                layoutLabel.setBounds (label.removeFromLeft (100));
                layouts.setBounds (label);
            }
        }

        void setBus (int busIdx)
        {
            if (busIdx != currentBus)
            {
                currentBus = busIdx;
                updateDisplay();
            }
        }

        void updateSupported()
        {
            viableLayouts.clear();
            if (AudioProcessor* processor = owner.getAudioProcessor())
            {
                const int n = processor->getBusCount (isInput);
                for (int busIdx = 0; busIdx < n; ++busIdx)
                {
                    Array<AudioChannelSet> supported;

                    if (AudioProcessor::AudioProcessorBus* bus = processor->getBus (isInput, busIdx))
                    {
                        for (int i = 0; i <= AudioChannelSet::maxChannelsOfNamedLayout; ++i)
                        {
                            AudioChannelSet set;

                            if      (bus->isLayoutSupported (set = AudioChannelSet::namedChannelSet(i)))
                                supported.addIfNotAlreadyThere (set);
                            else if (bus->isLayoutSupported (set = AudioChannelSet::discreteChannels (i)))
                                supported.addIfNotAlreadyThere (set);
                        }

                        supported.addIfNotAlreadyThere (bus->getLastEnabledLayout());
                        viableLayouts.add (supported);
                    }
                }
            }

            ioBuses.updateConfig();
        }

        void updateDisplay()
        {
            if (AudioProcessor* processor = owner.getAudioProcessor())
            {
                if (AudioProcessor::AudioProcessorBus* bus = processor->getBus (isInput, currentBus))
                {
                    ioBuses.setEnabled (true);
                    const Array<AudioChannelSet>& supported = viableLayouts.getReference (currentBus);
                    const AudioChannelSet& currentSet = currentLayouts.getReference (currentBus);

                    name.setText (bus->getName(), NotificationType::dontSendNotification);

                    if (supported.contains (AudioChannelSet()))
                    {
                        enabledToggle.setEnabled (true);
                        enabledToggle.setToggleState (enabledLayouts [currentBus], NotificationType::dontSendNotification);
                    }
                    else
                    {
                        enabledToggle.setEnabled (false);
                        enabledToggle.setToggleState (true, NotificationType::dontSendNotification);
                    }

                    layouts.clear();
                    layouts.setEnabled (true);

                    const int n = supported.size();
                    for (int i = 0; i < n; ++i)
                    {
                        const AudioChannelSet& set = supported.getReference (i);

                        if (! set.isDisabled())
                            layouts.addItem (set.getDescription(), i + 1);
                    }

                    if (! currentSet.isDisabled())
                    {
                        const int currentLayoutIdx = supported.indexOf (currentSet);
                        jassert (currentLayoutIdx != -1);

                        layouts.setSelectedId (currentLayoutIdx + 1, NotificationType::dontSendNotification);
                    }
                }
                else
                {
                    ioBuses.setEnabled (false);
                    layouts.clear();
                    name.setText ("", NotificationType::dontSendNotification);
                    enabledToggle.setEnabled (false);
                    layouts.setEnabled (false);
                }
            }
        }

        void updateBusConfig (const Array<AudioChannelSet>& busLayouts)
        {
            if (busLayouts != currentLayouts)
            {
                const bool numberOfBussesHasChanged = (busLayouts.size() != currentLayouts.size());

                enabledLayouts.clear();
                for (int i = 0; i < busLayouts.size(); ++i)
                {
                    const AudioChannelSet& newSet = busLayouts.getReference (i);

                    const bool enabled = ! newSet.isDisabled();
                    enabledLayouts.setBit (i, enabled);

                    if (enabled)
                    {
                        if (i >= currentLayouts.size())
                            currentLayouts.add (newSet);
                        else
                            currentLayouts.getReference (i) = newSet;
                    }
                    else
                    {
                        if (i >= currentLayouts.size())
                            currentLayouts.add (owner.getAudioProcessor()->getBus (isInput, i)->getDefaultLayout());
                    }
                }

                if (numberOfBussesHasChanged)
                {
                    updateSupported();
                    const int lastBus = currentLayouts.size() - 1;
                    setBus (jmin (lastBus, currentBus >= 0 ? currentBus : lastBus));
                }

                const Array<AudioChannelSet>& supported = viableLayouts.getReference (currentBus);
                const AudioChannelSet& currentSet = currentLayouts.getReference (currentBus);

                if (! enabledLayouts[currentBus])
                {
                    enabledToggle.setToggleState (false, NotificationType::dontSendNotification);
                }
                else
                {
                    enabledToggle.setToggleState (true, NotificationType::dontSendNotification);
                    const int currentLayoutIdx = supported.indexOf (currentSet);
                    jassert (currentLayoutIdx != -1);

                    layouts.setSelectedId (currentLayoutIdx + 1, NotificationType::dontSendNotification);
                }
            }
        }

        void comboBoxChanged (ComboBox*) override
        {
            const Array<AudioChannelSet>& supported = viableLayouts.getReference (currentBus);
            const int layoutIndex = layouts.getSelectedId() - 1;

            if (layoutIndex >= supported.size())
                return;

            const AudioChannelSet& set = supported.getReference (layoutIndex);
            AudioChannelSet& currentSet = currentLayouts.getReference (currentBus);

            if (set != currentSet)
            {
                const bool isEnabled = enabledLayouts[currentBus];

                if (!isEnabled)
                    currentSet = set;

                owner.updateConfig (isEnabled ? set : AudioChannelSet(), isInput, currentBus);
            }
        }

        void buttonClicked (Button*) override {}

        void buttonStateChanged (Button*) override
        {
            const AudioChannelSet& currentSet = currentLayouts.getReference (currentBus);
            const Array<AudioChannelSet>& supported = viableLayouts.getReference (currentBus);
            const bool shouldEnable = enabledToggle.getToggleState();

            if (enabledToggle.isEnabled() && shouldEnable != enabledLayouts [currentBus])
            {
                const int layoutIndex = layouts.getSelectedId() - 1;
                const AudioChannelSet requestedSet = supported.getReference (layoutIndex);

                if (requestedSet != currentSet || shouldEnable != enabledLayouts [currentBus])
                    owner.updateConfig (shouldEnable ? requestedSet : AudioChannelSet(), isInput, currentBus);
            }
        }

        //==============================================================================
        struct BusButtonContent : Component, Button::Listener
        {
            BusButtonContent (InputOutputConfig& parent, bool isInputToUse)
                : owner (parent), currentBus (0), lastNumBuses (0), plusBus ("+"), minusBus ("-"), isInput (isInputToUse)
            {
                {
                    plusBus. setConnectedEdges (Button::ConnectedOnLeft | Button::ConnectedOnRight | Button::ConnectedOnTop | Button::ConnectedOnBottom);
                    minusBus.setConnectedEdges (Button::ConnectedOnLeft | Button::ConnectedOnRight | Button::ConnectedOnTop | Button::ConnectedOnBottom);

                    addAndMakeVisible (plusBus);
                    addAndMakeVisible (minusBus);

                    plusBus. addListener (this);
                    minusBus.addListener (this);
                }

                setSize (channelButtonWidth, 40);
            }

            void updateConfig()
            {
                if (AudioProcessor* p = owner.owner.getAudioProcessor())
                {
                    const int numBuses = p->getBusCount (isInput);
                    if (lastNumBuses != numBuses)
                    {
                        lastNumBuses = numBuses;
                        buses.clear();

                        currentBus = jmin (numBuses - 1, currentBus);

                        for (int i = 0; i < numBuses; ++i)
                        {
                            TextButton* button = new TextButton (String (i + 1));
                            button->setConnectedEdges (Button::ConnectedOnLeft | Button::ConnectedOnRight | Button::ConnectedOnTop | Button::ConnectedOnBottom);
                            button->setRadioGroupId (1, NotificationType::dontSendNotification);
                            button->setClickingTogglesState (true);

                            Colour busColour = Colours::green.withRotatedHue (static_cast<float> (i) / 5.0f);
                            button->setColour (TextButton::buttonColourId, busColour);
                            button->setColour (TextButton::buttonOnColourId, busColour.withMultipliedBrightness (2.0f));
                            button->setTooltip (p->getBus (isInput, i)->getName());

                            button->setToggleState (i == currentBus, NotificationType::dontSendNotification);

                            addAndMakeVisible (buses.add (button));
                        }

                        for (int i = 0; i < numBuses; ++i)
                            buses[i]->addListener (this);
                    }

                    plusBus. setEnabled (p->canAddBus (isInput));
                    minusBus.setEnabled (numBuses > 1 && p->canRemoveBus (isInput));

                    setSize ((numBuses + 1) * channelButtonWidth, 60);
                    repaint();
                }
            }

            void buttonClicked (Button* btn) override
            {
                if      (btn == &plusBus)  owner.owner.addBus (isInput);
                else if (btn == &minusBus) owner.owner.removeBus (isInput);
            }

            void buttonStateChanged (Button* btn) override
            {
                int busIdx;

                if (btn->getToggleState())
                {
                    if (TextButton* textButton = dynamic_cast<TextButton*> (btn))
                    {
                        if (textButton->getToggleState() && (busIdx = buses.indexOf (textButton)) >= 0 && busIdx != currentBus)
                        {
                            currentBus = busIdx;
                            owner.setBus (busIdx);
                        }
                    }
                }
            }

            void paint (Graphics& g) override
            {
                g.fillAll (Colours::grey);
            }

            void resized() override
            {
                Rectangle<int> r = getLocalBounds();
                r.removeFromBottom (20);

                for (int i = 0; i < buses.size(); ++i)
                    buses[i]->setBounds (r.removeFromLeft (channelButtonWidth));

                minusBus.setBounds (r.removeFromLeft (channelButtonWidth >> 1));
                plusBus.setBounds (r.removeFromLeft (channelButtonWidth >> 1));
            }

            //==============================================================================
            InputOutputConfig& owner;
            static const int channelButtonWidth;
            int currentBus, lastNumBuses;

            OwnedArray<TextButton> buses;
            TextButton plusBus, minusBus;
            bool isInput;
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusButtonContent)
        };

        //==============================================================================
        struct BusButtonHolder : Component
        {
            BusButtonHolder (InputOutputConfig& parent, bool isInputBus)
                : content (parent, isInputBus)
            {
                viewport.setViewedComponent (&content, false);
                viewport.setScrollBarsShown (false, true);

                addAndMakeVisible (viewport);

                setSize (400, content.getHeight() + 20);
            }

            void paint (Graphics& g) override
            {
                g.fillAll (Colours::lightgrey);
                g.drawRect (getLocalBounds());
            }

            void updateConfig()
            {
                content.updateConfig();
            }

            void resized() override
            {
                viewport.setBounds (getLocalBounds().reduced (1));
            }

            //==============================================================================
            Viewport viewport;
            BusButtonContent content;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusButtonHolder)
        };

        //==============================================================================
        IOConfigurationAudioProcessorEditor& owner;
        Label ioTitle, nameLabel, name, layoutLabel;
        ToggleButton enabledToggle;
        ComboBox layouts;
        BusButtonHolder ioBuses;
        bool isInput;
        int currentBus;
        Array<Array<AudioChannelSet> > viableLayouts;
        Array<AudioChannelSet> currentLayouts;
        BigInteger enabledLayouts;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputOutputConfig)
    };

    //==============================================================================
    AudioProcessor::AudioBusesLayout currentLayout;
    Label title;
    ScopedPointer<InputOutputConfig> inConfig, outConfig;
    TextButton applyButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IOConfigurationAudioProcessorEditor)
};

const int IOConfigurationAudioProcessorEditor::InputOutputConfig::BusButtonContent::channelButtonWidth = 40;

//==============================================================================
PluginWindow* PluginWindow::getWindowFor (AudioProcessorGraph::Node* const node,
                                          WindowFormatType type,
                                          AudioProcessorGraph& audioGraph)
{
    jassert (node != nullptr);

    for (int i = activePluginWindows.size(); --i >= 0;)
        if (activePluginWindows.getUnchecked(i)->owner == node
             && activePluginWindows.getUnchecked(i)->type == type)
            return activePluginWindows.getUnchecked(i);

    AudioProcessor* processor = node->getProcessor();
    AudioProcessorEditor* ui = nullptr;

    if (type == Normal)
    {
        ui = processor->createEditorIfNeeded();

        if (ui == nullptr)
            type = Generic;
    }

    if (ui == nullptr)
    {
        if (type == Generic || type == Parameters)
            ui = new GenericAudioProcessorEditor (processor);
        else if (type == Programs)
            ui = new ProgramAudioProcessorEditor (processor);
        else if (type == AudioIO)
            ui = new IOConfigurationAudioProcessorEditor (processor);
    }

    if (ui != nullptr)
    {
        if (AudioPluginInstance* const plugin = dynamic_cast<AudioPluginInstance*> (processor))
            ui->setName (plugin->getName());

        return new PluginWindow (ui, node, type, audioGraph);
    }

    return nullptr;
}

PluginWindow::~PluginWindow()
{
    activePluginWindows.removeFirstMatchingValue (this);
    clearContentComponent();
}

void PluginWindow::moved()
{
    owner->properties.set (getLastXProp (type), getX());
    owner->properties.set (getLastYProp (type), getY());
}

void PluginWindow::closeButtonPressed()
{
    owner->properties.set (getOpenProp (type), false);
    delete this;
}

//==============================================================================
class PinComponent   : public Component,
                       public SettableTooltipClient
{
public:
    PinComponent (FilterGraph& graph_,
                  const uint32 filterID_, const int index_, const bool isInput_)
        : filterID (filterID_),
          index (index_),
          isInput (isInput_),
          busIdx (0),
          graph (graph_)
    {
        if (const AudioProcessorGraph::Node::Ptr node = graph.getNodeForId (filterID_))
        {
            String tip;

            if (index == FilterGraph::midiChannelNumber)
            {
                tip = isInput ? "MIDI Input"
                              : "MIDI Output";
            }
            else
            {
                const AudioProcessor& processor = *node->getProcessor();

                int channel;
                channel = processor.getOffsetInBusBufferForAbsoluteChannelIndex (isInput, index, busIdx);

                if (const AudioProcessor::AudioProcessorBus* bus = processor.getBus (isInput, busIdx))
                    tip = bus->getName() + String (": ")
                          + AudioChannelSet::getAbbreviatedChannelTypeName (bus->getCurrentLayout().getTypeOfChannel (channel));
                else
                    tip = (isInput ? "Main Input: "
                           : "Main Output: ") + String (index + 1);

            }

            setTooltip (tip);
        }

        setSize (16, 16);
    }

    void paint (Graphics& g) override
    {
        const float w = (float) getWidth();
        const float h = (float) getHeight();

        Path p;
        p.addEllipse (w * 0.25f, h * 0.25f, w * 0.5f, h * 0.5f);

        p.addRectangle (w * 0.4f, isInput ? (0.5f * h) : 0.0f, w * 0.2f, h * 0.5f);

        Colour colour = (index == FilterGraph::midiChannelNumber ? Colours::red : Colours::green);

        g.setColour (colour.withRotatedHue (static_cast<float> (busIdx) / 5.0f));
        g.fillPath (p);
    }

    void mouseDown (const MouseEvent& e) override
    {
        getGraphPanel()->beginConnectorDrag (isInput ? 0 : filterID,
                                             index,
                                             isInput ? filterID : 0,
                                             index,
                                             e);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        getGraphPanel()->dragConnector (e);
    }

    void mouseUp (const MouseEvent& e) override
    {
        getGraphPanel()->endDraggingConnector (e);
    }

    const uint32 filterID;
    const int index;
    const bool isInput;
    int busIdx;

private:
    FilterGraph& graph;

    GraphEditorPanel* getGraphPanel() const noexcept
    {
        return findParentComponentOfClass<GraphEditorPanel>();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PinComponent)
};

//==============================================================================
class FilterComponent    : public Component
{
public:
    FilterComponent (FilterGraph& graph_,
                     const uint32 filterID_)
        : graph (graph_),
          filterID (filterID_),
          numInputs (0),
          numOutputs (0),
          pinSize (16),
          font (13.0f, Font::bold),
          numIns (0),
          numOuts (0)
    {
        shadow.setShadowProperties (DropShadow (Colours::black.withAlpha (0.5f), 3, Point<int> (0, 1)));
        setComponentEffect (&shadow);

        setSize (150, 60);
    }

    ~FilterComponent()
    {
        deleteAllChildren();
    }

    void mouseDown (const MouseEvent& e) override
    {
        originalPos = localPointToGlobal (Point<int>());

        toFront (true);

        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem (1, "Delete this filter");
            m.addItem (2, "Disconnect all pins");
            m.addSeparator();
            m.addItem (3, "Show plugin UI");
            m.addItem (4, "Show all programs");
            m.addItem (5, "Show all parameters");
            m.addSeparator();
            m.addItem (6, "Configure Audio I/O");
            m.addItem (7, "Test state save/load");

            const int r = m.show();

            if (r == 1)
            {
                graph.removeFilter (filterID);
                return;
            }
            else if (r == 2)
            {
                graph.disconnectFilter (filterID);
            }
            else
            {
                if (AudioProcessorGraph::Node::Ptr f = graph.getNodeForId (filterID))
                {
                    AudioProcessor* const processor = f->getProcessor();
                    jassert (processor != nullptr);

                    if (r == 7)
                    {
                        MemoryBlock state;
                        processor->getStateInformation (state);
                        processor->setStateInformation (state.getData(), (int) state.getSize());
                    }
                    else
                    {
                        PluginWindow::WindowFormatType type = processor->hasEditor() ? PluginWindow::Normal
                                                                                     : PluginWindow::Generic;

                        switch (r)
                        {
                            case 4: type = PluginWindow::Programs; break;
                            case 5: type = PluginWindow::Parameters; break;
                            case 6: type = PluginWindow::AudioIO; break;

                            default: break;
                        };

                        if (PluginWindow* const w = PluginWindow::getWindowFor (f, type, graph.getGraph()))
                            w->toFront (true);
                    }
                }
            }
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
        {
            Point<int> pos (originalPos + Point<int> (e.getDistanceFromDragStartX(), e.getDistanceFromDragStartY()));

            if (getParentComponent() != nullptr)
                pos = getParentComponent()->getLocalPoint (nullptr, pos);

            graph.setNodePosition (filterID,
                                   (pos.getX() + getWidth() / 2) / (double) getParentWidth(),
                                   (pos.getY() + getHeight() / 2) / (double) getParentHeight());

            getGraphPanel()->updateComponents();
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
        {
            graph.setChangedFlag (true);
        }
        else if (e.getNumberOfClicks() == 2)
        {
            if (const AudioProcessorGraph::Node::Ptr f = graph.getNodeForId (filterID))
                if (PluginWindow* const w = PluginWindow::getWindowFor (f, PluginWindow::Normal, graph.getGraph()))
                    w->toFront (true);
        }
    }

    bool hitTest (int x, int y) override
    {
        for (int i = getNumChildComponents(); --i >= 0;)
            if (getChildComponent(i)->getBounds().contains (x, y))
                return true;

        return x >= 3 && x < getWidth() - 6 && y >= pinSize && y < getHeight() - pinSize;
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colours::lightgrey);

        const int x = 4;
        const int y = pinSize;
        const int w = getWidth() - x * 2;
        const int h = getHeight() - pinSize * 2;

        g.fillRect (x, y, w, h);

        g.setColour (Colours::black);
        g.setFont (font);
        g.drawFittedText (getName(), getLocalBounds().reduced (4, 2), Justification::centred, 2);

        g.setColour (Colours::grey);
        g.drawRect (x, y, w, h);
    }

    void resized() override
    {
        if (AudioProcessorGraph::Node::Ptr f = graph.getNodeForId (filterID))
        {
            if (AudioProcessor* const processor = f->getProcessor())
            {
                for (int i = 0; i < getNumChildComponents(); ++i)
                {
                    if (PinComponent* const pc = dynamic_cast<PinComponent*> (getChildComponent(i)))
                    {
                        const bool isInput = pc->isInput;
                        int busIdx, channelIdx;

                        channelIdx =
                            processor->getOffsetInBusBufferForAbsoluteChannelIndex (isInput, pc->index, busIdx);

                        const int total = isInput ? numIns : numOuts;
                        const int index = pc->index == FilterGraph::midiChannelNumber ? (total - 1) : pc->index;

                        const float totalSpaces = static_cast<float> (total) + (static_cast<float> (jmax (0, processor->getBusCount (isInput) - 1)) * 0.5f);
                        const float indexPos = static_cast<float> (index) + (static_cast<float> (busIdx) * 0.5f);

                        pc->setBounds (proportionOfWidth ((1.0f + indexPos) / (totalSpaces + 1.0f)) - pinSize / 2,
                                       pc->isInput ? 0 : (getHeight() - pinSize),
                                       pinSize, pinSize);
                    }
                }
            }
        }
    }

    void getPinPos (const int index, const bool isInput, float& x, float& y)
    {
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            if (PinComponent* const pc = dynamic_cast<PinComponent*> (getChildComponent(i)))
            {
                if (pc->index == index && isInput == pc->isInput)
                {
                    x = getX() + pc->getX() + pc->getWidth() * 0.5f;
                    y = getY() + pc->getY() + pc->getHeight() * 0.5f;
                    break;
                }
            }
        }
    }

    void update()
    {
        const AudioProcessorGraph::Node::Ptr f (graph.getNodeForId (filterID));

        if (f == nullptr)
        {
            delete this;
            return;
        }

        numIns = f->getProcessor()->getTotalNumInputChannels();
        if (f->getProcessor()->acceptsMidi())
            ++numIns;

        numOuts = f->getProcessor()->getTotalNumOutputChannels();
        if (f->getProcessor()->producesMidi())
            ++numOuts;

        int w = 100;
        int h = 60;

        w = jmax (w, (jmax (numIns, numOuts) + 1) * 20);

        const int textWidth = font.getStringWidth (f->getProcessor()->getName());
        w = jmax (w, 16 + jmin (textWidth, 300));
        if (textWidth > 300)
            h = 100;

        setSize (w, h);

        setName (f->getProcessor()->getName());

        {
            Point<double> p = graph.getNodePosition (filterID);
            setCentreRelative ((float) p.x, (float) p.y);
        }

        if (numIns != numInputs || numOuts != numOutputs)
        {
            numInputs = numIns;
            numOutputs = numOuts;

            deleteAllChildren();

            int i;
            for (i = 0; i < f->getProcessor()->getTotalNumInputChannels(); ++i)
                addAndMakeVisible (new PinComponent (graph, filterID, i, true));

            if (f->getProcessor()->acceptsMidi())
                addAndMakeVisible (new PinComponent (graph, filterID, FilterGraph::midiChannelNumber, true));

            for (i = 0; i < f->getProcessor()->getTotalNumOutputChannels(); ++i)
                addAndMakeVisible (new PinComponent (graph, filterID, i, false));

            if (f->getProcessor()->producesMidi())
                addAndMakeVisible (new PinComponent (graph, filterID, FilterGraph::midiChannelNumber, false));

            resized();
        }
    }

    FilterGraph& graph;
    const uint32 filterID;
    int numInputs, numOutputs;

private:
    int pinSize;
    Point<int> originalPos;
    Font font;
    int numIns, numOuts;
    DropShadowEffect shadow;

    GraphEditorPanel* getGraphPanel() const noexcept
    {
        return findParentComponentOfClass<GraphEditorPanel>();
    }

    FilterComponent (const FilterComponent&);
    FilterComponent& operator= (const FilterComponent&);
};

//==============================================================================
class ConnectorComponent   : public Component,
                             public SettableTooltipClient
{
public:
    ConnectorComponent (FilterGraph& graph_)
        : sourceFilterID (0),
          destFilterID (0),
          sourceFilterChannel (0),
          destFilterChannel (0),
          graph (graph_),
          lastInputX (0),
          lastInputY (0),
          lastOutputX (0),
          lastOutputY (0)
    {
        setAlwaysOnTop (true);
    }

    void setInput (const uint32 sourceFilterID_, const int sourceFilterChannel_)
    {
        if (sourceFilterID != sourceFilterID_ || sourceFilterChannel != sourceFilterChannel_)
        {
            sourceFilterID = sourceFilterID_;
            sourceFilterChannel = sourceFilterChannel_;
            update();
        }
    }

    void setOutput (const uint32 destFilterID_, const int destFilterChannel_)
    {
        if (destFilterID != destFilterID_ || destFilterChannel != destFilterChannel_)
        {
            destFilterID = destFilterID_;
            destFilterChannel = destFilterChannel_;
            update();
        }
    }

    void dragStart (int x, int y)
    {
        lastInputX = (float) x;
        lastInputY = (float) y;
        resizeToFit();
    }

    void dragEnd (int x, int y)
    {
        lastOutputX = (float) x;
        lastOutputY = (float) y;
        resizeToFit();
    }

    void update()
    {
        float x1, y1, x2, y2;
        getPoints (x1, y1, x2, y2);

        if (lastInputX != x1
             || lastInputY != y1
             || lastOutputX != x2
             || lastOutputY != y2)
        {
            resizeToFit();
        }
    }

    void resizeToFit()
    {
        float x1, y1, x2, y2;
        getPoints (x1, y1, x2, y2);

        const Rectangle<int> newBounds ((int) jmin (x1, x2) - 4,
                                        (int) jmin (y1, y2) - 4,
                                        (int) std::abs (x1 - x2) + 8,
                                        (int) std::abs (y1 - y2) + 8);

        if (newBounds != getBounds())
            setBounds (newBounds);
        else
            resized();

        repaint();
    }

    void getPoints (float& x1, float& y1, float& x2, float& y2) const
    {
        x1 = lastInputX;
        y1 = lastInputY;
        x2 = lastOutputX;
        y2 = lastOutputY;

        if (GraphEditorPanel* const hostPanel = getGraphPanel())
        {
            if (FilterComponent* srcFilterComp = hostPanel->getComponentForFilter (sourceFilterID))
                srcFilterComp->getPinPos (sourceFilterChannel, false, x1, y1);

            if (FilterComponent* dstFilterComp = hostPanel->getComponentForFilter (destFilterID))
                dstFilterComp->getPinPos (destFilterChannel, true, x2, y2);
        }
    }

    void paint (Graphics& g) override
    {
        if (sourceFilterChannel == FilterGraph::midiChannelNumber
             || destFilterChannel == FilterGraph::midiChannelNumber)
        {
            g.setColour (Colours::red);
        }
        else
        {
            g.setColour (Colours::green);
        }

        g.fillPath (linePath);
    }

    bool hitTest (int x, int y) override
    {
        if (hitPath.contains ((float) x, (float) y))
        {
            double distanceFromStart, distanceFromEnd;
            getDistancesFromEnds (x, y, distanceFromStart, distanceFromEnd);

            // avoid clicking the connector when over a pin
            return distanceFromStart > 7.0 && distanceFromEnd > 7.0;
        }

        return false;
    }

    void mouseDown (const MouseEvent&) override
    {
        dragging = false;
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging)
        {
            getGraphPanel()->dragConnector (e);
        }
        else if (e.mouseWasDraggedSinceMouseDown())
        {
            dragging = true;

            graph.removeConnection (sourceFilterID, sourceFilterChannel, destFilterID, destFilterChannel);

            double distanceFromStart, distanceFromEnd;
            getDistancesFromEnds (e.x, e.y, distanceFromStart, distanceFromEnd);
            const bool isNearerSource = (distanceFromStart < distanceFromEnd);

            getGraphPanel()->beginConnectorDrag (isNearerSource ? 0 : sourceFilterID,
                                                 sourceFilterChannel,
                                                 isNearerSource ? destFilterID : 0,
                                                 destFilterChannel,
                                                 e);
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (dragging)
            getGraphPanel()->endDraggingConnector (e);
    }

    void resized() override
    {
        float x1, y1, x2, y2;
        getPoints (x1, y1, x2, y2);

        lastInputX = x1;
        lastInputY = y1;
        lastOutputX = x2;
        lastOutputY = y2;

        x1 -= getX();
        y1 -= getY();
        x2 -= getX();
        y2 -= getY();

        linePath.clear();
        linePath.startNewSubPath (x1, y1);
        linePath.cubicTo (x1, y1 + (y2 - y1) * 0.33f,
                          x2, y1 + (y2 - y1) * 0.66f,
                          x2, y2);

        PathStrokeType wideStroke (8.0f);
        wideStroke.createStrokedPath (hitPath, linePath);

        PathStrokeType stroke (2.5f);
        stroke.createStrokedPath (linePath, linePath);

        const float arrowW = 5.0f;
        const float arrowL = 4.0f;

        Path arrow;
        arrow.addTriangle (-arrowL, arrowW,
                           -arrowL, -arrowW,
                           arrowL, 0.0f);

        arrow.applyTransform (AffineTransform()
                                .rotated (float_Pi * 0.5f - (float) atan2 (x2 - x1, y2 - y1))
                                .translated ((x1 + x2) * 0.5f,
                                             (y1 + y2) * 0.5f));

        linePath.addPath (arrow);
        linePath.setUsingNonZeroWinding (true);
    }

    uint32 sourceFilterID, destFilterID;
    int sourceFilterChannel, destFilterChannel;

private:
    FilterGraph& graph;
    float lastInputX, lastInputY, lastOutputX, lastOutputY;
    Path linePath, hitPath;
    bool dragging;

    GraphEditorPanel* getGraphPanel() const noexcept
    {
        return findParentComponentOfClass<GraphEditorPanel>();
    }

    void getDistancesFromEnds (int x, int y, double& distanceFromStart, double& distanceFromEnd) const
    {
        float x1, y1, x2, y2;
        getPoints (x1, y1, x2, y2);

        distanceFromStart = juce_hypot (x - (x1 - getX()), y - (y1 - getY()));
        distanceFromEnd = juce_hypot (x - (x2 - getX()), y - (y2 - getY()));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConnectorComponent)
};


//==============================================================================
GraphEditorPanel::GraphEditorPanel (FilterGraph& graph_)
    : graph (graph_)
{
    graph.addChangeListener (this);
    setOpaque (true);
}

GraphEditorPanel::~GraphEditorPanel()
{
    graph.removeChangeListener (this);
    draggingConnector = nullptr;
    deleteAllChildren();
}

void GraphEditorPanel::paint (Graphics& g)
{
    g.fillAll (Colours::white);
}

void GraphEditorPanel::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;

        if (MainHostWindow* const mainWindow = findParentComponentOfClass<MainHostWindow>())
        {
            mainWindow->addPluginsToMenu (m);

            const int r = m.show();

            createNewPlugin (mainWindow->getChosenType (r), e.x, e.y);
        }
    }
}

void GraphEditorPanel::createNewPlugin (const PluginDescription* desc, int x, int y)
{
    graph.addFilter (desc, x / (double) getWidth(), y / (double) getHeight());
}

FilterComponent* GraphEditorPanel::getComponentForFilter (const uint32 filterID) const
{
    for (int i = getNumChildComponents(); --i >= 0;)
    {
        if (FilterComponent* const fc = dynamic_cast<FilterComponent*> (getChildComponent (i)))
            if (fc->filterID == filterID)
                return fc;
    }

    return nullptr;
}

ConnectorComponent* GraphEditorPanel::getComponentForConnection (const AudioProcessorGraph::Connection& conn) const
{
    for (int i = getNumChildComponents(); --i >= 0;)
    {
        if (ConnectorComponent* const c = dynamic_cast<ConnectorComponent*> (getChildComponent (i)))
            if (c->sourceFilterID == conn.sourceNodeId
                 && c->destFilterID == conn.destNodeId
                 && c->sourceFilterChannel == conn.sourceChannelIndex
                 && c->destFilterChannel == conn.destChannelIndex)
                return c;
    }

    return nullptr;
}

PinComponent* GraphEditorPanel::findPinAt (const int x, const int y) const
{
    for (int i = getNumChildComponents(); --i >= 0;)
    {
        if (FilterComponent* fc = dynamic_cast<FilterComponent*> (getChildComponent (i)))
        {
            if (PinComponent* pin = dynamic_cast<PinComponent*> (fc->getComponentAt (x - fc->getX(),
                                                                                     y - fc->getY())))
                return pin;
        }
    }

    return nullptr;
}

void GraphEditorPanel::resized()
{
    updateComponents();
}

void GraphEditorPanel::changeListenerCallback (ChangeBroadcaster*)
{
    updateComponents();
}

void GraphEditorPanel::updateComponents()
{
    for (int i = getNumChildComponents(); --i >= 0;)
    {
        if (FilterComponent* const fc = dynamic_cast<FilterComponent*> (getChildComponent (i)))
            fc->update();
    }

    for (int i = getNumChildComponents(); --i >= 0;)
    {
        ConnectorComponent* const cc = dynamic_cast<ConnectorComponent*> (getChildComponent (i));

        if (cc != nullptr && cc != draggingConnector)
        {
            if (graph.getConnectionBetween (cc->sourceFilterID, cc->sourceFilterChannel,
                                            cc->destFilterID, cc->destFilterChannel) == nullptr)
            {
                delete cc;
            }
            else
            {
                cc->update();
            }
        }
    }

    for (int i = graph.getNumFilters(); --i >= 0;)
    {
        const AudioProcessorGraph::Node::Ptr f (graph.getNode (i));

        if (getComponentForFilter (f->nodeId) == 0)
        {
            FilterComponent* const comp = new FilterComponent (graph, f->nodeId);
            addAndMakeVisible (comp);
            comp->update();
        }
    }

    for (int i = graph.getNumConnections(); --i >= 0;)
    {
        const AudioProcessorGraph::Connection* const c = graph.getConnection (i);

        if (getComponentForConnection (*c) == 0)
        {
            ConnectorComponent* const comp = new ConnectorComponent (graph);
            addAndMakeVisible (comp);

            comp->setInput (c->sourceNodeId, c->sourceChannelIndex);
            comp->setOutput (c->destNodeId, c->destChannelIndex);
        }
    }
}

void GraphEditorPanel::beginConnectorDrag (const uint32 sourceFilterID, const int sourceFilterChannel,
                                           const uint32 destFilterID, const int destFilterChannel,
                                           const MouseEvent& e)
{
    draggingConnector = dynamic_cast<ConnectorComponent*> (e.originalComponent);

    if (draggingConnector == nullptr)
        draggingConnector = new ConnectorComponent (graph);

    draggingConnector->setInput (sourceFilterID, sourceFilterChannel);
    draggingConnector->setOutput (destFilterID, destFilterChannel);

    addAndMakeVisible (draggingConnector);
    draggingConnector->toFront (false);

    dragConnector (e);
}

void GraphEditorPanel::dragConnector (const MouseEvent& e)
{
    const MouseEvent e2 (e.getEventRelativeTo (this));

    if (draggingConnector != nullptr)
    {
        draggingConnector->setTooltip (String::empty);

        int x = e2.x;
        int y = e2.y;

        if (PinComponent* const pin = findPinAt (x, y))
        {
            uint32 srcFilter = draggingConnector->sourceFilterID;
            int srcChannel   = draggingConnector->sourceFilterChannel;
            uint32 dstFilter = draggingConnector->destFilterID;
            int dstChannel   = draggingConnector->destFilterChannel;

            if (srcFilter == 0 && ! pin->isInput)
            {
                srcFilter = pin->filterID;
                srcChannel = pin->index;
            }
            else if (dstFilter == 0 && pin->isInput)
            {
                dstFilter = pin->filterID;
                dstChannel = pin->index;
            }

            if (graph.canConnect (srcFilter, srcChannel, dstFilter, dstChannel))
            {
                x = pin->getParentComponent()->getX() + pin->getX() + pin->getWidth() / 2;
                y = pin->getParentComponent()->getY() + pin->getY() + pin->getHeight() / 2;

                draggingConnector->setTooltip (pin->getTooltip());
            }
        }

        if (draggingConnector->sourceFilterID == 0)
            draggingConnector->dragStart (x, y);
        else
            draggingConnector->dragEnd (x, y);
    }
}

void GraphEditorPanel::endDraggingConnector (const MouseEvent& e)
{
    if (draggingConnector == nullptr)
        return;

    draggingConnector->setTooltip (String::empty);

    const MouseEvent e2 (e.getEventRelativeTo (this));

    uint32 srcFilter = draggingConnector->sourceFilterID;
    int srcChannel   = draggingConnector->sourceFilterChannel;
    uint32 dstFilter = draggingConnector->destFilterID;
    int dstChannel   = draggingConnector->destFilterChannel;

    draggingConnector = nullptr;

    if (PinComponent* const pin = findPinAt (e2.x, e2.y))
    {
        if (srcFilter == 0)
        {
            if (pin->isInput)
                return;

            srcFilter = pin->filterID;
            srcChannel = pin->index;
        }
        else
        {
            if (! pin->isInput)
                return;

            dstFilter = pin->filterID;
            dstChannel = pin->index;
        }

        graph.addConnection (srcFilter, srcChannel, dstFilter, dstChannel);
    }
}


//==============================================================================
class TooltipBar   : public Component,
                     private Timer
{
public:
    TooltipBar()
    {
        startTimer (100);
    }

    void paint (Graphics& g) override
    {
        g.setFont (Font (getHeight() * 0.7f, Font::bold));
        g.setColour (Colours::black);
        g.drawFittedText (tip, 10, 0, getWidth() - 12, getHeight(), Justification::centredLeft, 1);
    }

    void timerCallback() override
    {
        Component* const underMouse = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
        TooltipClient* const ttc = dynamic_cast<TooltipClient*> (underMouse);

        String newTip;

        if (ttc != nullptr && ! (underMouse->isMouseButtonDown() || underMouse->isCurrentlyBlockedByAnotherModalComponent()))
            newTip = ttc->getTooltip();

        if (newTip != tip)
        {
            tip = newTip;
            repaint();
        }
    }

private:
    String tip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TooltipBar)
};

//==============================================================================
GraphDocumentComponent::GraphDocumentComponent (AudioPluginFormatManager& formatManager,
                                                AudioDeviceManager* deviceManager_)
    : graph (formatManager), deviceManager (deviceManager_),
      graphPlayer (getAppProperties().getUserSettings()->getBoolValue ("doublePrecisionProcessing", false))
{
    addAndMakeVisible (graphPanel = new GraphEditorPanel (graph));

    deviceManager->addChangeListener (graphPanel);

    graphPlayer.setProcessor (&graph.getGraph());

    keyState.addListener (&graphPlayer.getMidiMessageCollector());

    addAndMakeVisible (keyboardComp = new MidiKeyboardComponent (keyState,
                                                                 MidiKeyboardComponent::horizontalKeyboard));

    addAndMakeVisible (statusBar = new TooltipBar());

    deviceManager->addAudioCallback (&graphPlayer);
    deviceManager->addMidiInputCallback (String::empty, &graphPlayer.getMidiMessageCollector());

    graphPanel->updateComponents();
}

GraphDocumentComponent::~GraphDocumentComponent()
{
    deviceManager->removeAudioCallback (&graphPlayer);
    deviceManager->removeMidiInputCallback (String::empty, &graphPlayer.getMidiMessageCollector());
    deviceManager->removeChangeListener (graphPanel);

    deleteAllChildren();

    graphPlayer.setProcessor (nullptr);
    keyState.removeListener (&graphPlayer.getMidiMessageCollector());

    graph.clear();
}

void GraphDocumentComponent::resized()
{
    const int keysHeight = 60;
    const int statusHeight = 20;

    graphPanel->setBounds (0, 0, getWidth(), getHeight() - keysHeight);
    statusBar->setBounds (0, getHeight() - keysHeight - statusHeight, getWidth(), statusHeight);
    keyboardComp->setBounds (0, getHeight() - keysHeight, getWidth(), keysHeight);
}

void GraphDocumentComponent::createNewPlugin (const PluginDescription* desc, int x, int y)
{
    graphPanel->createNewPlugin (desc, x, y);
}
