#include <assert.h>
int __ctzsi2(unsigned int);
int __clzsi2(unsigned int);
int __popcountsi2(unsigned int);
int main(void) {
    assert(__ctzsi2(1u) == 0);
    assert(__ctzsi2(0x100u) == 8);
    assert(__ctzsi2(0u) == 32);
    assert(__clzsi2(1u) == 31);
    assert(__clzsi2(0x80000000u) == 0);
    assert(__clzsi2(0u) == 32);
    assert(__popcountsi2(0xf0f0u) == 8);
    assert(__popcountsi2(0u) == 0);
    return 0;
}
