#include <JuceHeader.h>
#include "MainComponent.h"
#include "DawLook.h"

class LDAWApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "L-DAW"; }
    const juce::String getApplicationVersion() override { return LDAW_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        look.setDefaultSansSerifTypefaceName ("Segoe UI");
        juce::LookAndFeel::setDefaultLookAndFeel (&look);   // one dark theme for the whole app
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override  { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name, juce::Colour (0xff1b1c1f), juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1100, 600, 5000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    DawLook look;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LDAWApplication)
