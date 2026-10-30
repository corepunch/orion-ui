import sys
import os

def read_pbm_p4(filename):
		with open(filename, 'rb') as f:
				magic = f.readline().strip()
				if magic != b'P4':
						raise ValueError('Not a binary PBM (P4) file')

				def read_non_comment_line():
						while True:
								line = f.readline()
								if not line.startswith(b'#'):
										return line
				
				size_line = read_non_comment_line()
				width, height = map(int, size_line.split())
				data = f.read(width * height)
				print(f"Image size: {width}x{height}")
				print(f"Data length: {len(data)} bytes")
				return data, width, height

def print_usage():
		print("Usage: python pbm_to_bitmask.py <input.pbm> <output.c>")
		print("Converts 1-bit binary PBM image into C array of 32-bit hex bitmasks.")

def main():
		if len(sys.argv) != 3:
				print_usage()
				return

		infile = sys.argv[1]
		outfile = sys.argv[2]

		try:
				data, width, height = read_pbm_p4(infile)
		except Exception as e:
				print(f"Error reading PBM file: {e}")
				return

		base = os.path.splitext(os.path.basename(infile))[0]
		varname = base.replace('-', '_').replace(' ', '_') + '_bits'

		bits_per_int = 4

		with open(outfile, 'w') as f:
				f.write(f"// Generated from {infile}\n")
				f.write(f"// Image size: {width}x{height}\n\n")
				f.write(f"unsigned char {varname}[] = {{\n")
				for i, byte in enumerate(data):
					for bit in range(7, -1, -1):
						bit_val = ((byte >> bit) & 1) * 255
						f.write(f"0x{(~bit_val&0xFF):02X},")
					if (i + 1) % bits_per_int == 0:
						f.write("\n")
				f.write("};\n")

if __name__ == "__main__":
		main()
