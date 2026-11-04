# Embedded Linux Systems - Assignment 02

**Student:** Le Tan Minh  
**Student ID:** SE200684  
**Project:** Sensor Monitoring System

## 1. Project Overview

This project implements a sensor monitoring system for Embedded Linux.
It includes a Linux kernel sensor driver, a userspace application,
host-side tests, and real-time jitter measurements.

## 2. Directory Structure

- `app/` - Userspace application source code.
- `driver/` - Sensor kernel driver and build files.
- `realtime/` - Real-time jitter test source code.
- `test_host/` - Host-side application and test files.
- `rootfs/` - Root filesystem for the embedded environment.
- `output/` - Build artifacts and initramfs.
- `logs/` - Test, debugging, and performance logs.
- `evidence/` - Supporting evidence.

## 3. Test Results

### Valgrind

The recorded test reports:
- 11 allocations and 11 frees.
- 0 bytes in use at exit.
- 0 detected errors.

### Real-Time Jitter

FIFO:
- Samples: 1000
- Average jitter: 16222.84 ns
- Maximum jitter: 99551 ns

OTHER:
- Samples: 1000
- Average jitter: 70758.65 ns
- Maximum jitter: 990504 ns

These values are from the recorded test logs.

## 4. Build and Test

Refer to the Makefiles and source files in the project directories.
Check the target environment and required dependencies before building.

## 5. Submission Checklist

- [ ] Verify all source files and build artifacts.
- [ ] Verify the complete test logs.
- [ ] Prepare the technical report in PDF format.
- [ ] Prepare the 3-5 minute demonstration video.
- [ ] Rebuild and verify the submission archive.
