#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace nedd::test
{
int renderUiSnapshots (const juce::File& directory);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    const int snapshotIndex = args.indexOf ("--snapshots");
    if (snapshotIndex >= 0)
    {
        const juce::File dir = args[snapshotIndex + 1].isNotEmpty()
                                   ? juce::File::getCurrentWorkingDirectory().getChildFile (args[snapshotIndex + 1])
                                   : juce::File::getCurrentWorkingDirectory().getChildFile ("snapshots");
        return nedd::test::renderUiSnapshots (dir);
    }

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);

    const int filterIndex = args.indexOf ("--test");
    if (filterIndex >= 0 && args[filterIndex + 1].isNotEmpty())
    {
        juce::Array<juce::UnitTest*> selected;
        for (auto* t : juce::UnitTest::getTestsInCategory ("NeddPE"))
            if (t->getName().containsIgnoreCase (args[filterIndex + 1]))
                selected.add (t);
        runner.runTests (selected);
    }
    else
    {
        runner.runTestsInCategory ("NeddPE");
    }

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
        std::cout << (r->failures > 0 ? "[FAIL] " : "[ ok ] ") << r->unitTestName << " / " << r->subcategoryName
                  << "  (" << r->passes << " passed, " << r->failures << " failed)\n";
        for (const auto& message : r->messages)
            std::cout << "        " << message << "\n";
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return failures > 0 ? 1 : 0;
}
