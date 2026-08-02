#include "TestSuites.hpp"
#include "support/UiTestSuites.hpp"

namespace wobble
{

int runUiTests(int argc, char **argv)
{
    int result = 0;
    result |= runUiShellTests(argc, argv);
    result |= runUiSelectionTests(argc, argv);
    result |= runUiViewportTests(argc, argv);
    result |= runUiDrawingToolTests(argc, argv);
    result |= runUiSessionTests(argc, argv);
    return result;
}

}
