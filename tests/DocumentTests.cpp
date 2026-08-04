#include "TestSuites.hpp"
#include "support/DocumentTestSuites.hpp"

namespace wobble
{

int runDocumentTests(int argc, char **argv)
{
    int result = 0;
    result |= runDocumentLifecycleTests(argc, argv);
    result |= runDocumentHistoryTests(argc, argv);
    result |= runLayerCommandTests(argc, argv);
    result |= runDocumentSchemaTests(argc, argv);
    result |= runDocumentResizeTests(argc, argv);
    result |= runStrokeCommandTests(argc, argv);
    result |= runSelectionClipboardTests(argc, argv);
    result |= runSerializationBudgetTests(argc, argv);
    result |= runRasterAssetTableTests(argc, argv);
    result |= runWawaV10ReaderTests(argc, argv);
    return result;
}

}
