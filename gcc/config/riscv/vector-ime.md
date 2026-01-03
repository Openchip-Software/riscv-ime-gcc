;; Machine description for RISC-V IME (Integrated Matrix Extension)
;; Copyright (C) 2026 Free Software Foundation, Inc.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; UNSPEC placeholders for the IME instructions.  Instruction patterns are
;; added as each IME instruction family is implemented; see the plan in
;; transient-marinating-cake.md for the rollout order.

(define_c_enum "unspec" [
  UNSPEC_VMMACC
])
