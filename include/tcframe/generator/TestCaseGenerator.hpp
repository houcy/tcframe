#pragma once

#include <iostream>
#include <functional>
#include <set>
#include <string>
#include <type_traits>

#include "GenerationException.hpp"
#include "GeneratorConfig.hpp"
#include "GeneratorLogger.hpp"
#include "tcframe/evaluator.hpp"
#include "tcframe/io_manipulator.hpp"
#include "tcframe/os.hpp"
#include "tcframe/spec.hpp"
#include "tcframe/verdict.hpp"
#include "tcframe/verifier.hpp"

using std::char_traits;
using std::endl;
using std::function;
using std::set;
using std::string;

namespace tcframe {

class TestCaseGenerator {
private:
    Verifier* verifier_;
    IOManipulator* ioManipulator_;
    OperatingSystem* os_;
    Evaluator* evaluator_;
    GeneratorLogger* logger_;

public:
    virtual ~TestCaseGenerator() {}

    TestCaseGenerator(
            Verifier* verifier,
            IOManipulator* ioManipulator,
            OperatingSystem* os,
            Evaluator* evaluator,
            GeneratorLogger* logger)
            : verifier_(verifier)
            , ioManipulator_(ioManipulator)
            , os_(os)
            , evaluator_(evaluator)
            , logger_(logger) {}

    virtual bool generate(const TestCase& testCase, const GeneratorConfig& config) {
        logger_->logTestCaseIntroduction(testCase.name());

        string inputFilename = config.outputDir() + "/" + testCase.name() + ".in";
        string outputFilename = config.outputDir() + "/" + testCase.name() + ".out";

        try {
            applyInput(testCase);
            verifyInput(testCase);
            generateInput(testCase, inputFilename, config);
            generateAndApplyOutput(testCase, inputFilename, outputFilename, config);
        } catch (GenerationException& e) {
            logger_->logTestCaseFailedResult(testCase.description());
            e.callback()();
            return false;
        } catch (runtime_error& e) {
            logger_->logTestCaseFailedResult(testCase.description());
            logger_->logSimpleFailure(e.what());
            return false;
        }

        logger_->logTestCaseSuccessfulResult();
        return true;
    }

private:
    void applyInput(const TestCase& testCase) {
        if (testCase.data()->type() == TestCaseDataType::SAMPLE) {
            SampleTestCaseData* data = (SampleTestCaseData*) testCase.data();
            istringstream input(data->input());
            ioManipulator_->parseInput(&input);
        } else {
            OfficialTestCaseData* data = (OfficialTestCaseData*) testCase.data();
            data->closure()();
        }
    }

    void verifyInput(const TestCase& testCase) {
        ConstraintsVerificationResult result = verifier_->verifyConstraints(testCase.subtaskIds());
        if (!result.isValid()) {
            throw GenerationException([=] {logger_->logConstraintsVerificationFailure(result);});
        }
    }

    void generateInput(const TestCase& testCase, const string& inputFilename, const GeneratorConfig& config) {
        ostream* input = os_->openForWriting(inputFilename);
        modifyInputForMultipleTestCases(input, config);

        if (testCase.data()->type() == TestCaseDataType::SAMPLE) {
            SampleTestCaseData* data = (SampleTestCaseData*) testCase.data();
            *input << data->input();
        } else {
            ioManipulator_->printInput(input);
        }
        os_->closeOpenedStream(input);
    }

    void generateAndApplyOutput(
            const TestCase& testCase,
            const string& inputFilename,
            const string& outputFilename,
            const GeneratorConfig& config) {

        optional<string> maybeSampleOutputString = getSampleOutputString(testCase);
        if (!config.needsOutput()) {
            if (maybeSampleOutputString) {
                throw GenerationException([=] { logger_->logSampleTestCaseNoOutputNeededFailure(); });
            }
            return;
        }

        EvaluatorConfig evaluatorConfig = EvaluatorConfigBuilder()
                .setSolutionCommand(config.solutionCommand())
                .build();

        GenerationResult generationResult = evaluator_->generate(inputFilename, outputFilename, evaluatorConfig);
        if (!generationResult.executionResult().isSuccessful()) {
            throw GenerationException([=] {
                logger_->logExecutionResults({{"solution", generationResult.executionResult()}});
            });
        }

        if (maybeSampleOutputString) {
            checkSampleOutput(maybeSampleOutputString.value(), inputFilename, outputFilename, config);
        }

        istream* output = os_->openForReading(outputFilename);
        modifyOutputForMultipleTestCases(output, config);
        ioManipulator_->parseOutput(output);
        os_->closeOpenedStream(output);
    }

    optional<string> getSampleOutputString(const TestCase& testCase) {
        if (testCase.data()->type() != TestCaseDataType::SAMPLE) {
            return optional<string>();
        }
        return ((SampleTestCaseData*) testCase.data())->output();
    }

    void modifyInputForMultipleTestCases(ostream* input, const GeneratorConfig& config) {
        if (config.multipleTestCasesCounter() != nullptr) {
            int testCaseId = 1;
            *input << testCaseId << endl;
        }
    }

    void modifySampleOutputStringForMultipleTestCases(string& outputString, const GeneratorConfig& config) {
        if (config.multipleTestCasesCounter() != nullptr && config.multipleTestCasesFirstOutputPrefix()) {
            outputString = config.multipleTestCasesFirstOutputPrefix().value() + outputString;
        }
    }

    void modifyOutputForMultipleTestCases(istream* output, const GeneratorConfig& config) {
        if (config.multipleTestCasesCounter() != nullptr && config.multipleTestCasesFirstOutputPrefix()) {
            string prefix = config.multipleTestCasesOutputPrefix().value();
            string firstPrefix = config.multipleTestCasesFirstOutputPrefix().value();
            for (char p : firstPrefix) {
                int c = output->peek();
                if (c == char_traits<char>::eof() || (char) c != p) {
                    throw runtime_error("Output must start with \"" + prefix + "\"");
                }
                output->get();
            }
        }
    }

    void checkSampleOutput(
            const string& sampleOutputString,
            const string& inputFilename,
            const string& outputFilename,
            const GeneratorConfig& config) {

        string modifiedSampleOutputString = sampleOutputString;
        modifySampleOutputStringForMultipleTestCases(modifiedSampleOutputString, config);

        ostream* sampleOutput = os_->openForWriting(Evaluator::EVALUATION_OUT_FILENAME);
        *sampleOutput << modifiedSampleOutputString;
        os_->closeOpenedStream(sampleOutput);

        ScoringResult scoringResult = evaluator_->score(inputFilename, outputFilename);
        if (!(scoringResult.verdict().status() == VerdictStatus::ac())) {
            throw GenerationException([=] {
                logger_->logSampleTestCaseCheckFailure();
                logger_->logExecutionResults({{"scorer", scoringResult.executionResult()}});
            });
        }
    }
};

}
