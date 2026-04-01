#!/usr/bin/env python3
import socket
import sys

def send_command(sock, cmd):
    sock.sendall((cmd + "\r\n").encode())
    response = sock.recv(8192).decode()
    return response.strip()

def test_basic_commands():
    print("=== Testing Basic KV Commands ===")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 19999))
    sock.settimeout(5)
    
    tests = [
        # Array engine
        ("SET key1 value1", "OK"),
        ("GET key1", "value1"),
        ("EXIST key1", "EXIST"),
        ("MOD key1 value2", "OK"),
        ("GET key1", "value2"),
        ("DEL key1", "OK"),
        ("GET key1", "NO EXIST"),
        
        # Hash engine
        ("HSET hkey1 hvalue1", "OK"),
        ("HGET hkey1", "hvalue1"),
        ("HDEL hkey1", "OK"),
        
        # RBTree engine
        ("RSET rkey1 rvalue1", "OK"),
        ("RGET rkey1", "rvalue1"),
        ("RDEL rkey1", "OK"),
        
        # SkipList engine
        ("SSET skey1 svalue1", "OK"),
        ("SGET skey1", "svalue1"),
        ("SDEL skey1", "OK"),
        
        # Cache commands
        ("POLICY", None),
        ("STATS", None),
        ("RECOMMEND", None),
    ]
    
    passed = 0
    failed = 0
    
    for cmd, expected in tests:
        try:
            response = send_command(sock, cmd)
            if expected is None:
                print(f"  [PASS] {cmd} -> {response[:50]}...")
                passed += 1
            elif expected in response:
                print(f"  [PASS] {cmd} -> {response}")
                passed += 1
            else:
                print(f"  [FAIL] {cmd} -> Expected: {expected}, Got: {response}")
                failed += 1
        except Exception as e:
            print(f"  [ERROR] {cmd} -> {e}")
            failed += 1
    
    sock.close()
    
    print(f"\n=== Results: {passed} passed, {failed} failed ===")
    return failed == 0

def test_cache_integration():
    print("\n=== Testing Cache Integration ===")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 19999))
    sock.settimeout(5)
    
    passed = 0
    failed = 0
    
    # Test 1: Setup test data
    print("\n  --- Test 1: Setup test data ---")
    send_command(sock, "SET cache_test_key cache_test_value")
    print("    [INFO] SET cache_test_key cache_test_value")
    
    # Test 2: First GET (should be cache miss)
    print("\n  --- Test 2: First GET (cache miss) ---")
    response = send_command(sock, "GET cache_test_key")
    if "cache_test_value" in response:
        print(f"    [PASS] GET cache_test_key -> {response}")
        passed += 1
    else:
        print(f"    [FAIL] GET cache_test_key -> {response}")
        failed += 1
    
    # Test 3: Second GET (should be cache hit)
    print("\n  --- Test 3: Second GET (should be cache hit) ---")
    response = send_command(sock, "GET cache_test_key")
    if "cache_test_value" in response:
        print(f"    [PASS] GET cache_test_key -> {response}")
        passed += 1
    else:
        print(f"    [FAIL] GET cache_test_key -> {response}")
        failed += 1
    
    # Test 4: Check cache stats
    print("\n  --- Test 4: Check cache stats ---")
    response = send_command(sock, "STATS")
    print(f"    [INFO] STATS response:\n{response}")
    if "Cache Hits:" in response and "Cache Miss" in response:
        print("    [PASS] STATS contains hit/miss info")
        passed += 1
    else:
        print("    [FAIL] STATS missing hit/miss info")
        failed += 1
    
    sock.close()
    
    print(f"\n=== Cache Integration Test Results: {passed} passed, {failed} failed ===")
    return failed == 0

def test_arc_policy():
    print("\n=== Testing ARC Policy ===")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 19999))
    sock.settimeout(5)
    
    passed = 0
    failed = 0
    
    # Test 1: Set policy to ARC
    print("\n  --- Test 1: Set policy to ARC ---")
    response = send_command(sock, "POLICY SET arc")
    if "OK" in response or "ARC" in response:
        print(f"    [PASS] POLICY SET arc -> {response}")
        passed += 1
    else:
        print(f"    [FAIL] POLICY SET arc -> {response}")
        failed += 1
    
    # Test 2: Verify policy is ARC
    print("\n  --- Test 2: Verify policy is ARC ---")
    response = send_command(sock, "POLICY")
    if "ARC" in response:
        print(f"    [PASS] POLICY -> {response}")
        passed += 1
    else:
        print(f"    [FAIL] POLICY -> {response}")
        failed += 1
    
    # Test 3: Insert multiple keys
    print("\n  --- Test 3: Insert multiple keys for ARC ---")
    for i in range(10):
        send_command(sock, f"SET arc_key_{i} arc_value_{i}")
    print("    [INFO] Inserted 10 keys")
    
    # Test 4: Access some keys multiple times (to move to T2)
    print("\n  --- Test 4: Access some keys multiple times ---")
    for _ in range(3):
        for i in range(5):  # Access first 5 keys multiple times
            send_command(sock, f"GET arc_key_{i}")
    print("    [INFO] Accessed keys 0-4 multiple times")
    
    # Test 5: Check ARC stats
    print("\n  --- Test 5: Check ARC stats ---")
    response = send_command(sock, "STATS")
    print(f"    [INFO] STATS response:\n{response}")
    if "T1 Size:" in response and "T2 Size:" in response:
        print("    [PASS] STATS shows ARC T1/T2 info")
        passed += 1
    else:
        print("    [FAIL] STATS missing ARC T1/T2 info")
        failed += 1
    
    # Test 6: Verify T2 has entries (frequently accessed)
    print("\n  --- Test 6: Verify T2 has entries ---")
    if "T2 Size:" in response:
        # Extract T2 size
        lines = response.split('\n')
        t2_size = 0
        for line in lines:
            if "T2 Size:" in line:
                parts = line.split(':')
                if len(parts) >= 2:
                    t2_size = int(parts[1].strip().split()[0])
                    break
        if t2_size > 0:
            print(f"    [PASS] T2 has {t2_size} entries (frequently accessed)")
            passed += 1
        else:
            print(f"    [FAIL] T2 is empty")
            failed += 1
    else:
        print("    [FAIL] Could not find T2 Size")
        failed += 1
    
    # Test 7: Test cache hit with ARC
    print("\n  --- Test 7: Test cache hit with ARC ---")
    response = send_command(sock, "GET arc_key_0")
    if "arc_value_0" in response:
        print(f"    [PASS] GET arc_key_0 -> {response}")
        passed += 1
    else:
        print(f"    [FAIL] GET arc_key_0 -> {response}")
        failed += 1
    
    # Test 8: Test cache delete with ARC
    print("\n  --- Test 8: Test cache delete with ARC ---")
    send_command(sock, "SET arc_del_key arc_del_value")
    response = send_command(sock, "GET arc_del_key")
    if "arc_del_value" in response:
        send_command(sock, "DEL arc_del_key")
        response = send_command(sock, "GET arc_del_key")
        if "NO EXIST" in response:
            print("    [PASS] DEL removes from ARC cache")
            passed += 1
        else:
            print(f"    [FAIL] DEL did not remove from cache -> {response}")
            failed += 1
    else:
        print(f"    [FAIL] Could not set up test key")
        failed += 1
    
    # Test 9: Switch back to LRU
    print("\n  --- Test 9: Switch back to LRU ---")
    response = send_command(sock, "POLICY SET lru")
    if "OK" in response or "LRU" in response:
        print(f"    [PASS] POLICY SET lru -> {response}")
        passed += 1
    else:
        print(f"    [FAIL] POLICY SET lru -> {response}")
        failed += 1
    
    sock.close()
    
    print(f"\n=== ARC Policy Test Results: {passed} passed, {failed} failed ===")
    return failed == 0

if __name__ == "__main__":
    try:
        success1 = test_basic_commands()
        # success2 = test_vector_operations()
        success3 = test_cache_integration()
        success4 = test_arc_policy()

        print("\n" + "="*50)
        print("FINAL SUMMARY")
        print("="*50)
        print(f"Basic Commands:    {'PASS' if success1 else 'FAIL'}")
        # print(f"Vector Operations: {'PASS' if success2 else 'FAIL'}")
        print(f"Cache Integration: {'PASS' if success3 else 'FAIL'}")
        print(f"ARC Policy:        {'PASS' if success4 else 'FAIL'}")
        print("="*50)
        
        sys.exit(0 if (success1 and success3 and success4) else 1)
    except Exception as e:
        print(f"Test failed with error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
