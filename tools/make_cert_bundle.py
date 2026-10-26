#!/usr/bin/env python3
"""Builds data/cert/x509_crt_bundle.bin, the root certificates the device trusts for HTTPS (firmware updates).

    python3 tools/make_cert_bundle.py path/to/cacert.pem [-o data/cert/x509_crt_bundle.bin]

The input is a PEM list of root certificates, for example Mozilla's, published at https://curl.se/ca/cacert.pem.
The output is the format Arduino's WiFiClientSecure::setCACertBundle() reads: a big-endian 16-bit count, then for
every certificate, sorted by its subject name, 16-bit subject length, 16-bit key length, the subject (DER) and
the public key (DER, SubjectPublicKeyInfo). Only the standard library is needed.
"""
import argparse
import base64
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PEM = re.compile(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", re.S)


def tlv(buf, pos):
    """Returns (tag, value_start, value_end) of the DER element starting at pos."""
    tag = buf[pos]
    length = buf[pos + 1]
    pos += 2
    if length & 0x80:
        n = length & 0x7F
        length = int.from_bytes(buf[pos:pos + n], "big")
        pos += n
    return tag, pos, pos + length


def children(buf, start, end):
    """Spans (from, to) of the elements directly inside buf[start:end]."""
    out = []
    pos = start
    while pos < end:
        _, vs, ve = tlv(buf, pos)
        out.append((pos, ve))
        pos = ve
    return out


def subject_and_key(der):
    _, cs, ce = tlv(der, 0)                    # Certificate
    tbs = children(der, cs, ce)[0]             # tbsCertificate
    _, ts, te = tlv(der, tbs[0])
    parts = children(der, ts, te)
    if der[parts[0][0]] == 0xA0:               # the optional [0] version
        parts = parts[1:]
    # serialNumber, signature, issuer, validity, subject, subjectPublicKeyInfo
    subject = der[parts[4][0]:parts[4][1]]
    key = der[parts[5][0]:parts[5][1]]
    return subject, key


def build(pem_text):
    certs = []
    for block in PEM.findall(pem_text):
        der = base64.b64decode("".join(block.split()))
        certs.append(subject_and_key(der))
    certs.sort(key=lambda c: c[0])
    out = struct.pack(">H", len(certs))
    for subject, key in certs:
        out += struct.pack(">HH", len(subject), len(key)) + subject + key
    return out, len(certs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pem")
    ap.add_argument("-o", "--output", default=str(ROOT / "data" / "cert" / "x509_crt_bundle.bin"))
    args = ap.parse_args()
    data, n = build(pathlib.Path(args.pem).read_text())
    if n == 0:
        sys.exit("no certificates found in " + args.pem)
    out = pathlib.Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"wrote {out} ({n} certificates, {len(data)} bytes)")


if __name__ == "__main__":
    main()
