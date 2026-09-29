"""Synthetic parser regressions; no copyrighted game fixtures required."""
import struct
import unittest

from ecstatica_decode import DecodeError, Reader, actors, events, fant, pixels


def event(opcode=0, index=0, a=0, b=0, c=0):
    return struct.pack('>5h', opcode, index, a, b, c)


def sample(action=None, codes=b'\0\0', sounds=b'\0'):
    header = b'FANT' + struct.pack('>hh', 30, 1) + bytes(26)
    return header + (action or event()) + event() * 2 + codes + event() + sounds + b'\0\0'


class DecoderTests(unittest.TestCase):
    def test_empty_record(self):
        data = sample()
        self.assertEqual(fant(data)['consumed'], len(data))

    def test_every_truncated_prefix_is_rejected(self):
        data = sample()
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises(DecodeError):
                fant(data[:size])

    def test_terminator_operands_are_preserved(self):
        value = fant(sample(event(0, -1, 12, -300, 32767)))['action_events'][0]
        self.assertEqual(value['values'], [12, -300, 32767])
        self.assertEqual(value['index'], -1)

    def test_next_scene_variable_attachment(self):
        stream = event(27, 1) + b'abcde\0' + event(1, 2, -3, 4, -5) + event()
        parsed = fant(sample(stream))['action_events']
        self.assertEqual(parsed[0]['attachment'], 'abcde')
        self.assertEqual(parsed[1]['offset'], 50)
        self.assertEqual(parsed[1]['values'], [-3, 4, -5])
        self.assertEqual(len(events(Reader(event(27, 0) + event()))), 2)

    def test_unknown_opcode_and_version_fail(self):
        with self.assertRaises(DecodeError):
            fant(sample(event(99)))
        data = bytearray(sample())
        data[5] = 29
        with self.assertRaises(DecodeError):
            fant(data)

    def test_string_token_even_padding_and_source_lines(self):
        codes = struct.pack('>hhH', 1, 3, 0xe003) + b'abcd'
        codes += struct.pack('>Hhh', 0x1001, 0, 1) + b'line\0'
        decoded = fant(sample(codes=codes))['codes'][0]
        self.assertEqual(decoded['index'], 3)
        self.assertEqual(decoded['tokens'][0]['string_hex'], b'abcd'.hex())
        self.assertEqual(decoded['tokens'][1]['value'], 0x1001)
        self.assertEqual(decoded['lines'], ['line'])

    def test_sound_is_little_endian(self):
        sound = b'\1' + struct.pack('<hHhih', 123, 0x8002, -2, 3, 100) + b'xyz' + b'\0'
        decoded = fant(sample(sounds=sound))['sounds'][0]
        self.assertEqual((decoded['index'], decoded['flags'], decoded['unknown1']), (123, 32770, -2))
        self.assertEqual(decoded['payload_hex'], b'xyz'.hex())
        malformed = b'\1' + struct.pack('<hHhih', 0, 0, 0, -1, 0)
        with self.assertRaises(DecodeError):
            fant(sample(sounds=malformed))

    def test_reader_unterminated_and_negative_reads(self):
        with self.assertRaises(DecodeError):
            Reader(b'abc').string()
        with self.assertRaises(DecodeError):
            Reader(b'abc').take(-1)

    def test_hierarchy_and_exact_dimensions(self):
        data = event(8, 0, 0) + event(10, 0, 2) + event(4, 2, 68, 112, 56)
        data += event(7, 2, 1) + event(4, 1, 64, 68, 48) + event()
        decoded = actors(events(Reader(data)), {'parts': ['unused', 'pec', 'chest']})[0]['parts']
        self.assertEqual(decoded[1]['parent'], 2)
        self.assertEqual(decoded[1]['fields']['halfaxes']['values'], [64, 68, 48])
        self.assertEqual(decoded[1]['fields']['halfaxes']['offset'], 40)

    def test_hierarchy_cycle_and_missing_parent_rejected(self):
        for parent in (1, 99):
            with self.assertRaises(DecodeError):
                actors(events(Reader(event(8) + event(7, parent, 1) + event())), {'parts': []})

    def test_colour_delta_nibble_order_wrap_and_repeat(self):
        # 1 literal 250, then +7, -8, -1, producing 1, 249, 248.
        data = bytes([6, 250, 12, 0x87, 0x0f])
        remaining = 64000 - 4
        while remaining:
            n = min(63, remaining)
            data += bytes([(n << 2) | 3, 42])
            remaining -= n
        decoded = pixels(data + b'\0')
        self.assertEqual(decoded[:4], [250, 1, 249, 248])
        self.assertEqual(decoded[-1], 42)
        with self.assertRaises(DecodeError):
            pixels(data + bytes([7, 1]))

    def test_depth_delta_and_word_scaling(self):
        data = bytearray([6, 0xff, 0x3f, 5, 1, 4, 0x0f])
        remaining = 64000 - 3
        while remaining:
            n = min(63, remaining)
            data.extend([(n << 2) | 3, 2, 0])
            remaining -= n
        decoded = pixels(bytes(data), True)
        self.assertEqual(decoded[:3], [65532, 0, 65532])
        self.assertEqual(decoded[-1], 8)

    def test_short_pixels_rejected(self):
        with self.assertRaises(DecodeError):
            pixels(bytes([0]))


if __name__ == '__main__':
    unittest.main()
