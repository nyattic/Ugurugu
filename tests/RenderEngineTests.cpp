#include "TestSuites.hpp"
#include "support/RenderTestSuites.hpp"

namespace wobble
{

int runRenderEngineTests(int argc, char **argv)
{
    int result = 0;
    result |= runRenderPreviewTests(argc, argv);
    result |= runWobbleAnimationTests(argc, argv);
    result |= runLayerCompositionTests(argc, argv);
    result |= runStrokeCoverageTests(argc, argv);
    result |= runLayerSplitPreviewTests(argc, argv);
    result |= runLegacyRenderGoldenTests(argc, argv);
    result |= runStrokeRenderingTests(argc, argv);
    result |= runSelectionPreviewTests(argc, argv);
    result |= runBrushRenderingTests(argc, argv);
    return result;
}

}
