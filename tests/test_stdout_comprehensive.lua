-- More comprehensive test for stdout interception
-- Tests various write operations

-- Test 1: print() variations
print("Line 1")
print("Line", "2", "with", "multiple", "args")

-- Test 2: io.write() variations
io.write("Line 3\n")
io.write("Line ", "4", " concatenated\n")

-- Test 3: io.stdout:write() variations
io.stdout:write("Line 5 from io.stdout:write\n")

-- Test 4: Mixed operations
print("Before file write")

-- Write to a file - this should NOT appear in terminal buffer
-- Use current directory for cross-platform compatibility
local f = io.open("test_file_output.txt", "w")
if f then
  f:write("This is file content line 1\n")
  f:write("This is file content line 2\n")
  f:close()
  print("File write completed")
end

-- Read from a file - this should work normally
f = io.open("test_file_output.txt", "r")
if f then
  f:read("*a")
  f:close()
  -- The file content is verified in the C test code
  print("File read completed")
end

print("Test complete")
