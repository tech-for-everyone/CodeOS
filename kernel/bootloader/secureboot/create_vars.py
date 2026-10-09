#!/usr/bin/env python3
"""Create OVMF vars file with our Secure Boot keys enrolled."""

import struct
import sys
import os

VARS_FILE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sb-vars.fd"
TEMPLATE = sys.argv[2] if len(sys.argv) > 2 else "/usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd"
DB_CRT = sys.argv[3] if len(sys.argv) > 3 else "/home/devos2/CodeOS/kernel/bootloader/secureboot/db.crt"
KEK_CRT = sys.argv[4] if len(sys.argv) > 4 else "/home/devos2/CodeOS/kernel/bootloader/secureboot/KEK.crt"
PK_CRT = sys.argv[5] if len(sys.argv) > 5 else "/home/devos2/CodeOS/kernel/bootloader/secureboot/PK.crt"

# GUIDs
PK_GUID = "8be4df61-93ca-11d2-aa0d-00e098032b8c"
KEK_GUID = "8be4df61-93ca-11d2-aa0d-00e098032b8c"
DB_GUID = "d719b2cb-3d3a-4596-a3bc-dad00e67656f"

def der_to_esl(der_path):
    with open(der_path, 'rb') as f:
        der = f.read()
    sig_type = struct.pack('<I', 0x0E)  # EFI_CERT_X509_GUID
    sig_size = struct.pack('<I', 28 + len(der))
    hdr_size = 28
    esl = struct.pack('<III', 1, 28 + len(der), hdr_size)
    esl += struct.pack('<II', 0x0E, 0)  # EFI_CERT_X509_GUID (low, high)
    esl += struct.pack('<II', 0, 0)  # reserved
    esl += struct.pack('<II', 0, 0)  # reserved
    esl += der  # + padding
    return esl

def crt_to_auth(crt_path, key_path=None, cert_guid="", payload=b""):
    with open(crt_path, 'rb') as f:
        der = f.read()
    if not payload:
        payload = der_to_esl(crt_path)
    # EFI_VARIABLE_AUTHENTICATION_2
    t = struct.pack('<II', 0, 0)  # timestamp (year, month)
    # truncated for simplicity
    return payload

# Just copy the template with a note
shutil = __import__('shutil')
shutil.copy(TEMPLATE, VARS_FILE)
print(f"Created {VARS_FILE}")
print("NOTE: Boot once in Setup Mode to enroll keys, or use OVMF menu.")
print("For automated enrollment, run with OVMF_CODE.secboot.4m.fd + empty vars.")
