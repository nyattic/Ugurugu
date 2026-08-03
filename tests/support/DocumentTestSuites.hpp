#pragma once

namespace wobble
{

int runDocumentLifecycleTests(int argc, char **argv);
int runDocumentHistoryTests(int argc, char **argv);
int runLayerCommandTests(int argc, char **argv);
int runDocumentSchemaTests(int argc, char **argv);
int runDocumentResizeTests(int argc, char **argv);
int runStrokeCommandTests(int argc, char **argv);
int runSelectionClipboardTests(int argc, char **argv);
int runSerializationBudgetTests(int argc, char **argv);
int runRasterAssetTableTests(int argc, char **argv);

}
