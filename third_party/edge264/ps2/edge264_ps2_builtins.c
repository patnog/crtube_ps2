/*
 * GCC libgcc-compatible bit-operation helpers for the PS2 EE.
 * Avoid compiler builtins so these routines cannot lower recursively.
 */
int __ctzsi2(unsigned int value)
{
    int count = 0;
    if (value == 0)
        return 32;
    while ((value & 1u) == 0u) {
        value >>= 1;
        ++count;
    }
    return count;
}

int __clzsi2(unsigned int value)
{
    int count = 0;
    if (value == 0)
        return 32;
    while ((value & 0x80000000u) == 0u) {
        value <<= 1;
        ++count;
    }
    return count;
}

int __popcountsi2(unsigned int value)
{
    int count = 0;
    while (value) {
        value &= value - 1u;
        ++count;
    }
    return count;
}
