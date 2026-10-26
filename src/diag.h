#pragma once

// Writes a line to the serial console and to the in-memory event log (GET /api/log).
void diagf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
