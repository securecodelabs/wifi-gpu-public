"""Exhaustive basis/truth-table proofs for the opt-in Mesa NIR rewrites."""
from itertools import product


def main():
    mask = (1 << 32) - 1
    # The shifted fields are disjoint, so OR is XOR and both sides are linear
    # over GF(2). Zero plus all 64 basis vectors proves every pair of words.
    for shift in range(1, 32):
        for bit in range(-1, 64):
            value = 0 if bit < 0 else 1 << bit
            a, b = value & mask, (value >> 32) & mask
            original = ((b << (32 - shift)) | (a >> shift)) & mask
            funnel = (((b << 32) | a) >> shift) & mask
            assert original == funnel, (shift, bit)
    # Bitwise operations act independently on each bit. Test both source
    # patterns and the replacement against the majority truth table.
    for a, b, c in product((0, 1), repeat=3):
        expected = int(a + b + c >= 2)
        for op in (a ^ b, a | b):
            assert ((a & b) | (c & op)) == expected
        select = a ^ b
        assert ((select & c) | ((~select) & a)) == expected
    print('PASS: 31 funnel shifts on zero and all 64 basis vectors; majority truth table')


if __name__ == '__main__':
    main()
