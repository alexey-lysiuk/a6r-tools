#!/usr/bin/env python3
"""
Comprehensive test suite for SD card commands
Tests: sd_list, sd_write, sd_read, sd_delete
"""

import serial
import time
import sys
import struct

class SDCardTester:
    def __init__(self, port='COM11', baudrate=115200, timeout=2):
        """Initialize serial connection to device"""
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        time.sleep(0.5)  # Let device settle
        self.test_files = []
        
        # Clear any pending data
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        print(f"Connected to {port} at {baudrate} baud\n")
    
    def send_command(self, cmd):
        """Send command and discard echo"""
        self.ser.write(f"{cmd}\r\n".encode('ascii'))
        time.sleep(0.05)  # Give device time to echo
        # Read and discard the command echo
        if self.ser.in_waiting:
            echo = self.ser.readline()  # Read echoed command line
        print(echo)
    
    def wait_for_prompt(self, timeout=5.0):
        """Wait for the 'ch> ' prompt signaling ready for next command"""
        start = time.time()
        buffer = b''
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                byte = self.ser.read(1)
                buffer += byte
                # Check for prompt at end of buffer
                if buffer.endswith(b'ch> '):
                    return True
            else:
                time.sleep(0.01)
        return False
    
    def read_until_prompt(self, timeout=5.0):
        """Read all data until 'ch> ' prompt is received"""
        start = time.time()
        data = b''
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                byte = self.ser.read(1)
                data += byte
                # Check if we got the prompt
                if data.endswith(b'ch> '):
                    # Remove the prompt from the data
                    return data[:-4].decode('ascii', errors='ignore')
            else:
                time.sleep(0.01)
        
        # Timeout - return what we have
        return data.decode('ascii', errors='ignore')
    
    def read_response(self, timeout=1.0, skip_prompt=True):
        """Read all available response data, optionally skipping prompt"""
        start = time.time()
        data = b''
        while time.time() - start < timeout:
            if self.ser.in_waiting:
                chunk = self.ser.read(self.ser.in_waiting)
                data += chunk
                time.sleep(0.05)  # Small delay for more data
            elif data:
                break  # Got some data and no more coming
        
        response = data.decode('ascii', errors='ignore')
        
        # Remove prompt characters if present
        if skip_prompt:
            response = response.replace('ch> ', '')
        
        return response
    
    def test_sd_list(self, pattern='*.*'):
        """Test sd_list command"""
        print(f"TEST: sd_list {pattern}")
        self.send_command(f"sd_list {pattern}")
        response = self.read_until_prompt()
        print(f"Response:\n{response}")
        
        # Parse file list
        files = []
        for line in response.split('\n'):
            line = line.strip()
            if line and not line.startswith('>') and not line.startswith('err'):
                parts = line.split()
                if len(parts) >= 2:
                    files.append({'name': parts[0], 'size': int(parts[1])})
        
        print(f"Found {len(files)} files\n")
        return files
    
    def test_sd_write(self, filename, data):
        """Test sd_write command with binary data"""
        print(f"TEST: sd_write {filename} ({len(data)} bytes)")
        
        # Send command
        cmd = f"sd_write {filename} {len(data)}"
        self.send_command(cmd)
        
        # Wait for response: should be byte count followed by prompt
        # Format: "COUNT\n> "
        response = b''
        while not response.endswith(b'ch> '):
            if self.ser.in_waiting:
                response += self.ser.read(1)
            else:
                time.sleep(0.01)
        
        response_str = response[:-4].decode('ascii', errors='ignore').strip()
        print(f"Response: {response_str}")
        
        # Verify byte count matches
        try:
            byte_count = int(response_str)
            if byte_count != len(data):
                print(f"ERROR: Byte count mismatch - expected {len(data)}, device reported {byte_count}\n")
                return False
        except ValueError:
            print(f"ERROR: Could not parse byte count from '{response_str}'\n")
            return False
        
        # Send binary data in chunks
        chunk_size = 64
        bytes_sent = 0
        while bytes_sent < len(data):
            chunk_end = min(bytes_sent + chunk_size, len(data))
            chunk = data[bytes_sent:chunk_end]
            self.ser.write(chunk)
            bytes_sent += len(chunk)
            time.sleep(0.01)  # Small delay between chunks
        
        # Wait for completion: just a newline, no "OK written" message
        time.sleep(0.1)  # Give device time to finish writing
        completion = self.ser.read(self.ser.in_waiting).decode('ascii', errors='ignore')
        
        # Success if we didn't get an error message
        success = not completion.startswith('err')
        if success:
            print(f"SUCCESS: Wrote {len(data)} bytes\n")
            self.test_files.append(filename)
        else:
            print(f"FAILED: {completion}\n")
        
        return success
    
    def test_sd_read(self, filename, expected_size=None):
        """Test sd_read command"""
        print(f"TEST: sd_read {filename}")
        
        self.send_command(f"sd_read {filename}")
        
        # # Response format is: "SIZE\n> " followed by file data
        # # Read until we get the prompt
        # response = b''
        # while not response.endswith(b'ch> '):
        #     if self.ser.in_waiting:
        #         response += self.ser.read(1)
        #     else:
        #         time.sleep(0.01)
        
        # Parse the size from response (everything before '\nch> ')
        # response_str = response[:-4].decode('ascii', errors='ignore').strip()
        # print(f"Response: {response_str}")
        
        size_binary = self.ser.read(4)
        if size_binary == b'err:':
            print(f"ERROR: File not found\n")
            return None

        size = struct.unpack('<1I', size_binary)[0]
        
        # Read file data (binary)
        data = b''
        start = time.time()
        timeout = 5.0 + (size / 1000)  # Dynamic timeout based on size
        
        while len(data) < size and time.time() - start < timeout:
            if self.ser.in_waiting:
                chunk = self.ser.read(min(self.ser.in_waiting, size - len(data)))
                data += chunk
            else:
                time.sleep(0.01)
        
        print(f"Read {len(data)} bytes")
        
        if expected_size is not None and len(data) != expected_size:
            print(f"WARNING: Expected {expected_size} bytes, got {len(data)}\n")
        elif len(data) == size:
            print(f"SUCCESS: Read complete\n")
        else:
            print(f"ERROR: Incomplete read (expected {size}, got {len(data)})\n")
        
        return data
    
    def test_sd_delete(self, pattern):
        """Test sd_delete command"""
        print(f"TEST: sd_delete {pattern}")
        
        self.send_command(f"sd_delete {pattern}")
        response = self.read_until_prompt()
        print(f"Response:\n{response}")
        
        success = 'OK' in response or 'delete:' in response
        if success:
            print(f"SUCCESS: Delete command completed\n")
            if pattern in self.test_files:
                self.test_files.remove(pattern)
        else:
            print(f"FAILED: {response}\n")
        
        return success
    
    def run_comprehensive_test(self):
        """Run comprehensive test suite"""
        print("="*60)
        print("SD CARD COMPREHENSIVE TEST SUITE")
        print("="*60 + "\n")
        
        results = {
            'passed': 0,
            'failed': 0,
            'tests': []
        }
        
        # Test 1: List files initially
        print("-" * 60)
        try:
            files_before = self.test_sd_list()
            results['tests'].append(('sd_list initial', True))
            results['passed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_list initial', False))
            results['failed'] += 1
        
        # # Test 2: Write small text file
        print("-" * 60)
        test_data_small = b"Hello, SD Card!\nThis is a test file.\n"
        try:
            success = self.test_sd_write('test_small.txt', test_data_small)
            results['tests'].append(('sd_write small text', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_write small text', False))
            results['failed'] += 1
        
        # Test 3: Read back small file
        print("-" * 60)
        try:
            data_read = self.test_sd_read('test_small.txt', len(test_data_small))
            success = data_read == test_data_small
            if success:
                print("Data verification: PASSED")
            else:
                print("Data verification: FAILED")
                print(f"Expected: {test_data_small[:50]}")
                print(f"Got:      {data_read[:50] if data_read else 'None'}")
            print()
            results['tests'].append(('sd_read small text', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_read small text', False))
            results['failed'] += 1
        
        # Test 4: Write binary file
        print("-" * 60)
        test_data_binary = bytes(range(256)) * 4  # 1KB of binary data
        try:
            success = self.test_sd_write('test_binary.bin', test_data_binary)
            results['tests'].append(('sd_write binary', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_write binary', False))
            results['failed'] += 1
        
        # Test 5: Read back binary file
        print("-" * 60)
        try:
            data_read = self.test_sd_read('test_binary.bin', len(test_data_binary))
            success = data_read == test_data_binary
            if success:
                print("Binary data verification: PASSED")
            else:
                print("Binary data verification: FAILED")
                if data_read:
                    # Show first difference
                    for i in range(min(len(test_data_binary), len(data_read))):
                        if test_data_binary[i] != data_read[i]:
                            print(f"First difference at byte {i}: expected {test_data_binary[i]:02x}, got {data_read[i]:02x}")
                            break
            print()
            results['tests'].append(('sd_read binary', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_read binary', False))
            results['failed'] += 1
        
        # Test 6: Write larger file (10KB)
        print("-" * 60)
        test_data_large = bytes([i % 256 for i in range(10240)])
        try:
            success = self.test_sd_write('test_large.bin', test_data_large)
            results['tests'].append(('sd_write large (10KB)', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_write large (10KB)', False))
            results['failed'] += 1
        
        # Test 7: Read back large file
        print("-" * 60)
        try:
            data_read = self.test_sd_read('test_large.bin', len(test_data_large))
            success = data_read == test_data_large
            if success:
                print("Large file verification: PASSED")
            else:
                print("Large file verification: FAILED")
            print()
            results['tests'].append(('sd_read large (10KB)', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_read large (10KB)', False))
            results['failed'] += 1
        
        # Test 8: List files after writes
        print("-" * 60)
        try:
            files_after = self.test_sd_list('test_*')
            success = len(files_after) >= 3
            results['tests'].append(('sd_list after writes', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_list after writes', False))
            results['failed'] += 1
        
        # Test 9: Delete specific file
        print("-" * 60)
        try:
            success = self.test_sd_delete('test_small.txt')
            results['tests'].append(('sd_delete specific file', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_delete specific file', False))
            results['failed'] += 1
        
        # Test 10: Delete with wildcard
        print("-" * 60)
        try:
            success = self.test_sd_delete('test_*.bin')
            results['tests'].append(('sd_delete wildcard', success))
            if success:
                results['passed'] += 1
            else:
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('sd_delete wildcard', False))
            results['failed'] += 1
        
        # Test 11: Verify files deleted
        print("-" * 60)
        try:
            files_final = self.test_sd_list('test_*')
            success = len(files_final) == 0
            results['tests'].append(('verify deletion', success))
            if success:
                results['passed'] += 1
            else:
                print(f"WARNING: {len(files_final)} test files remain")
                results['failed'] += 1
        except Exception as e:
            print(f"EXCEPTION: {e}\n")
            results['tests'].append(('verify deletion', False))
            results['failed'] += 1
        
        # Print summary
        print("="*60)
        print("TEST SUMMARY")
        print("="*60)
        for test_name, passed in results['tests']:
            status = "✓ PASS" if passed else "✗ FAIL"
            print(f"{status:8} - {test_name}")
        print("-"*60)
        print(f"Total: {results['passed']} passed, {results['failed']} failed")
        print("="*60 + "\n")
        
        return results['failed'] == 0
    
    def cleanup(self):
        """Clean up test files"""
        if self.test_files:
            print("Cleaning up test files...")
            for filename in self.test_files:
                try:
                    self.test_sd_delete(filename)
                except:
                    pass
    
    def close(self):
        """Close serial connection"""
        self.ser.close()

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_sd_card.py <COM_PORT> [baudrate]")
        print("Example: python test_sd_card.py COM3 115200")
        sys.exit(1)
    
    port = sys.argv[1]
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    
    tester = None
    try:
        tester = SDCardTester(port, baudrate)
        success = tester.run_comprehensive_test()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n\nFATAL ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        if tester:
            tester.close()

if __name__ == '__main__':
    main()
