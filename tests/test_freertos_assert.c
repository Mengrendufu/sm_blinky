#include <assert.h>
#include <string.h>

#include "FreeRTOSConfig.h"

static unsigned int assertCount;
static char const *assertModule;
static int assertLabel;

void SM_onAssert(char const *module, int label)
{
    ++assertCount;
    assertModule = module;
    assertLabel = label;
}

int main(void)
{
    configASSERT(1);
    assert(assertCount == 0U);

    int const expectedLabel = __LINE__ + 1;
    configASSERT(0);
    assert(assertCount == 1U);
    assert(strcmp(assertModule, __FILE__) == 0);
    assert(assertLabel == expectedLabel);

    return 0;
}
