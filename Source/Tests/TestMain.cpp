#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace nedd::test
{
int renderUiSnapshots (const juce::File& directory);
int validateVst3 (const juce::File& file);
int runBenchmark();
int dumpParameters (const juce::File& file);
int exportPresets (const juce::File& directory);
}

namespace
{
    /** Prints JUCE log output (including assertion failures in Debug builds) and counts assertions. */
    struct ConsoleLogger : public juce::Logger
    {
        int assertions = 0;
        void logMessage (const juce::String& message) override
        {
            if (message.contains ("JUCE Assertion failure"))
                ++assertions;
            std::cout << message << std::endl;
        }
    };
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);
    struct ResetLogger { ~ResetLogger() { juce::Logger::setCurrentLogger (nullptr); } } resetLogger;

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

    const int validateIndex = args.indexOf ("--validate-vst3");
    if (validateIndex >= 0)
        return nedd::test::validateVst3 (juce::File::getCurrentWorkingDirectory().getChildFile (args[validateIndex + 1]));

    if (args.contains ("--bench"))
        return nedd::test::runBenchmark();

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    if (const int i = args.indexOf ("--dump-params"); i >= 0)
        return nedd::test::dumpParameters (cwd.getChildFile (args[i + 1].isNotEmpty() ? args[i + 1] : "PARAMETERS.md"));
    if (const int i = args.indexOf ("--export-presets"); i >= 0)
        return nedd::test::exportPresets (cwd.getChildFile (args[i + 1].isNotEmpty() ? args[i + 1] : "presets"));

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

    std::cout << "\n" << passes << " checks passed, " << failures << " failed";
    if (logger.assertions > 0)
        std::cout << ", " << logger.assertions << " JUCE assertion(s) fired";
    std::cout << "\n";
    return failures > 0 || logger.assertions > 0 ? 1 : 0;
}
