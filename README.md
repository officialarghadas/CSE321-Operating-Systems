# CSE321 — Operating Systems

Coursework repository for **CSE321 (Operating Systems)**. This repo collects assignments, lab exam materials, and a small filesystem-related project. Most of the code is written in **C**, with some **Makefile**, **Assembly**, and small **Python/Perl** components.

- **Repository:** https://github.com/officialarghadas/CSE321-Operating-Systems  
- **Default branch:** `main`  
- **Visibility:** Public  
- **License:** MIT (see [`LICENSE`](LICENSE))

## Language composition

- C (93%)
- Makefile (2.2%)
- Assembly (2.2%)
- Python (1.8%)
- Linker Script (0.5%)
- Perl (0.3%)

## Repository structure

High-level folders currently present at the repo root:

- `Assignment 1/`
  - `Assignment 1.pdf` — assignment handout/specification
  - `problem1.c` — solution/source for problem 1
  - `problem 2.c` — solution/source for problem 2
  - `.vscode/` — editor configuration
- `Assignment 2/`
  - `xv6-riscv/` — a RISC‑V port of the educational xv6 OS, plus a patch file
    - `24341220.patch` — patch file included alongside xv6
    - `Makefile`, `kernel/`, `user/`, `test-xv6.py`, etc.
    - `README` (upstream xv6 README with build/run notes)
- `VSFS Project/`
  - `VSFS_Project/` — project files (C sources + Makefile + generated artifacts)
- `Lab Exam/`
  - `LAB Exam 2/` — lab exam materials

## Assignment 1

Contains C implementations corresponding to the assignment tasks.

**Files:**
- `Assignment 1/problem1.c`
- `Assignment 1/problem 2.c`
- `Assignment 1/Assignment 1.pdf`

### How to build/run (Assignment 1)
If each `.c` file is a standalone program, you can typically compile with:

```bash
gcc -O2 -Wall -Wextra "Assignment 1/problem1.c" -o problem1
./problem1

gcc -O2 -Wall -Wextra "Assignment 1/problem 2.c" -o problem2
./problem2
```

(Adjust compiler flags/input arguments based on what the assignment PDF requires.)

## Assignment 2 (xv6-riscv)

This folder includes an `xv6-riscv` tree (educational OS for RISC‑V). The included `xv6-riscv/README` describes xv6 as:

- a re-implementation of UNIX Version 6 (v6)
- built for a modern **RISC‑V multiprocessor**
- written in **ANSI C**

### Build & run (xv6-riscv)
Per the included xv6 README:

- You’ll need a **RISC‑V GNU toolchain** (with “newlib”) and **QEMU** for `riscv64-softmmu`.
- Then you can run:

```bash
cd "Assignment 2/xv6-riscv"
make qemu
```

## VSFS Project

Located under `VSFS Project/VSFS_Project`.

**Notable files:**
- `Makefile`
- `journal.c`
- `mkfs.c`
- `validator.c`

**Also included (generated/binary artefacts):**
- `mkfs` (binary)
- `validator.exe`, `mkfs.exe`, `journal.exe`
- `vsfs.img` (filesystem image)

### Build (VSFS Project)
If the Makefile is set up for your environment:

```bash
cd "VSFS Project/VSFS_Project"
make
```

## Lab Exam

Located under `Lab Exam/` (currently shows `LAB Exam 2/`). This folder is intended for lab exam-related materials.

## Issues / Pull Requests

This repository currently has **0 open issues** and **0 open pull requests**.

## Notes

- Some folders (e.g., xv6) may include upstream code and licensing in addition to the repository’s MIT license at the root.

## License

This project is licensed under the **MIT License** — see the [`LICENSE`](LICENSE) file for details.
