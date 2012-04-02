//===- PTXInstPrinter.h - Convert PTX MCInst to assembly syntax -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints n PTX MCInst to a .ptx file.
//
//===----------------------------------------------------------------------===//

#ifndef PTXINSTPRINTER_H
#define PTXINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {

class MCOperand;

class PTXInstPrinter : public MCInstPrinter {
public:
  PTXInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                 const MCRegisterInfo &MRI, const MCSubtargetInfo &STI);

  virtual void printInst(const MCInst *MI, raw_ostream &O, StringRef Annot);
  virtual StringRef getOpcodeName(unsigned Opcode) const;
  virtual void printRegName(raw_ostream &OS, unsigned RegNo) const;

  // Autogenerated by tblgen.
  void printInstruction(const MCInst *MI, raw_ostream &O);
  static const char *getRegisterName(unsigned RegNo);

  void printPredicate(const MCInst *MI, raw_ostream &O);
  void printCall(const MCInst *MI, raw_ostream &O);
  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printMemOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);
  void printRoundingMode(const MCInst *MI, unsigned OpNo, raw_ostream &O);
};
}

#endif

