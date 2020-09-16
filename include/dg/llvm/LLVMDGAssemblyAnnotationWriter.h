#ifndef _LLVM_DG_ASSEMBLY_ANNOTATION_WRITER_H_
#define _LLVM_DG_ASSEMBLY_ANNOTATION_WRITER_H_


// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/FormattedStream.h>

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
 #include <llvm/Assembly/AssemblyAnnotationWriter.h>
 #include <llvm/Analysis/Verifier.h>
#else // >= 3.5
 #include <llvm/IR/AssemblyAnnotationWriter.h>
 #include <llvm/IR/Verifier.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/DataDependence.h"

namespace dg {
namespace debug {

class LLVMDGAssemblyAnnotationWriter : public llvm::AssemblyAnnotationWriter
{
    using LLVMDataDependenceAnalysis = dg::dda::LLVMDataDependenceAnalysis;
public:
    enum AnnotationOptsT {
        // data dependencies
        ANNOTATE_DD                 = 1 << 0,
        // forward data dependencies
        ANNOTATE_FORWARD_DD         = 1 << 1,
        // control dependencies
        ANNOTATE_CD                 = 1 << 2,
        // points-to information
        ANNOTATE_PTR                = 1 << 3,
        // reaching definitions
        ANNOTATE_DU                 = 1 << 4,
        // post-dominators
        ANNOTATE_POSTDOM            = 1 << 5,
        // comment out nodes that will be sliced
        ANNOTATE_SLICE              = 1 << 6,
        // annotate memory accesses (like ANNOTATE_PTR,
        // but with byte intervals)
        ANNOTATE_MEMORYACC          = 1 << 7,
    };

private:

    AnnotationOptsT opts;
    LLVMPointerAnalysis *PTA;
    LLVMDataDependenceAnalysis *DDA;
    const std::set<LLVMNode *> *criteria;
    std::string module_comment{};

    void printValue(const llvm::Value *val,
                    llvm::formatted_raw_ostream& os,
                    bool nl = false)
    {
        if (val->hasName())
            os << val->getName().data();
        else
            os << *val;

        if (nl)
            os << "\n";
    }

    void printPointer(const LLVMPointer& ptr,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = "PTR: ", bool nl = true) {
        os << "  ; ";
        if (prefix)
            os << prefix;

        printValue(ptr.value, os);

        os << " + ";
        if (ptr.offset.isUnknown())
            os << "?";
        else
            os << *ptr.offset;

        if (nl)
            os << "\n";
    }

    void printDefSite(const dda::DefSite& ds,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = nullptr, bool nl = false)
    {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (ds.target) {
            const llvm::Value *val = ds.target->getUserData<llvm::Value>();
            if (ds.target->isUnknown())
                os << "unknown";
            else
                printValue(val, os);

            if (ds.offset.isUnknown())
                os << " bytes |?";
            else
                os << " bytes |" << *ds.offset;

            if (ds.len.isUnknown())
                os << " - ?|";
            else
                os << " - " <<  *ds.offset + *ds.len- 1 << "|";
        } else
            os << "target is null!";

        if (nl)
            os << "\n";

    }

    void printMemRegion(const llvm::Instruction *I,
                        const LLVMMemoryRegion& R,
                        llvm::formatted_raw_ostream& os,
                        const char *prefix = nullptr,
                        bool nl = false) {
        os << "  ; ";
        if (prefix)
            os << prefix;

        assert(R.pointer.value);
        printValue(R.pointer.value, os);

        // TEST - RPW.
        auto startByte = *R.pointer.offset;
        auto endByte = *R.pointer.offset + *R.len - 1;
        llvm::errs() << "[RPW-DEBUG] The function: " << I->getFunction()->getName() << " accesses memory at: "
                        << R.pointer.value << " at byte range: " << " ["
                        << startByte << ", " << endByte << "]\n";

        const llvm::Function *fn = I->getFunction();
        for (auto arg = fn->arg_begin(); arg != fn->arg_end(); ++arg) {
            //if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(arg))
            //    llvm::errs() << "[RPW-DEBUG] " << ci->getValue() << "\n";
            llvm::errs() << "\t[RPW-DEBUG] Arguments: " << *arg << "\n";
        }

        // use-def chain.
        // All values that a particular instruction uses.
        /*for (const llvm::Use &U : I->operands()) {
            llvm::Value *v = U.get();
            //llvm::errs() << "[RPW-DEBUG] " << *v << "\n";
            os << "\n  ; [RPW-USE-DEF] " << *v;
        }*/

        // def-use chain.
        // All the instructions that use a given function.
        /*for (const llvm::User *U : fn->users()) {
            if (const llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(U)) {
                llvm::errs() << "[RPW-DEBUG] " << fn->getName() << " is used in instruction: " << *inst << "\n";
            }
        }*/

        // END TEST - RPW.

        if (R.pointer.offset.isUnknown())
            os << " bytes [?";
        else
            os << " bytes [" << *R.pointer.offset;

        if (R.len.isUnknown())
            os << " - ?]";
        else
            os << " - " <<  *R.pointer.offset + *R.len - 1 << "]";

        if (nl)
            os << "\n";
    }

    void emitNodeAnnotations(LLVMNode *node, llvm::formatted_raw_ostream& os)
    {
        using namespace llvm;

        if (opts & ANNOTATE_DU) {
            assert(DDA && "No reaching definitions analysis");
            /*

            // FIXME

            LLVMDGParameters *params = node->getParameters();
            // don't dump params when we use new analyses (DDA is not null)
            // because there we don't add definitions with new analyses
            if (params) {
                for (auto& it : *params) {
                    os << "  ; PARAMS: in " << it.second.in
                       << ", out " << it.second.out << "\n";

                    // dump edges for parameters
                    os <<"  ; in edges\n";
                    emitNodeAnnotations(it.second.in, os);
                    os << "  ; out edges\n";
                    emitNodeAnnotations(it.second.out, os);
                    os << "\n";
                }

                for (auto it = params->global_begin(), et = params->global_end();
                     it != et; ++it) {
                    os << "  ; PARAM GL: in " << it->second.in
                       << ", out " << it->second.out << "\n";

                    // dump edges for parameters
                    os << "  ; in edges\n";
                    emitNodeAnnotations(it->second.in, os);
                    os << "  ; out edges\n";
                    emitNodeAnnotations(it->second.out, os);
                    os << "\n";
                }
            }
            */
        }

        if (opts & ANNOTATE_DD) {
            for (auto I = node->rev_data_begin(), E = node->rev_data_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; DD: ";

                if (d->hasName())
                    os << d->getName();
                else
                    os << *d;

                os << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_FORWARD_DD) {
            for (auto I = node->data_begin(), E = node->data_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; fDD: " << *d << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_CD) {
            for (auto I = node->rev_control_begin(), E = node->rev_control_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; rCD: ";

                if (d->hasName())
                    os << d->getName() << "\n";
                else
                    os << *d << "\n";
            }
        }

        if (opts & ANNOTATE_PTR) {
            if (PTA) {
                llvm::Type *Ty = node->getKey()->getType();
                if (Ty->isPointerTy() || Ty->isIntegerTy()) {
                    const auto& ps = PTA->getLLVMPointsTo(node->getKey());
                    if (!ps.empty()) {
                        for (const auto& llvmptr : ps) {
                            printPointer(llvmptr, os);
                        }
                        if (ps.hasNull()) {
                            os << "  ; null\n";
                        }
                        if (ps.hasUnknown()) {
                            os << "  ; unknown\n";
                        }
                        if (ps.hasInvalidated()) {
                            os << "  ; invalidated\n";
                        }
                    }
                }
            }
        }

        // Update here? - RPW.
        if (PTA && (opts & ANNOTATE_MEMORYACC)) {
            if (auto I = dyn_cast<Instruction>(node->getValue())) {
                if (I->mayReadOrWriteMemory()) {
                    auto regions = PTA->getAccessedMemory(I);
                    if (regions.first) {
                            os << "  ; unknown region\n";
                    }
                    for (const auto& mem : regions.second) {
                        printMemRegion(I, mem, os, nullptr, true);
                    }
                }
            }
        }

        if (opts & ANNOTATE_SLICE) {
            if (criteria && criteria->count(node) > 0)
                os << "  ; SLICING CRITERION\n";
            if (node->getSlice() == 0)
                os << "  ; x ";
        }
    }

public:
    LLVMDGAssemblyAnnotationWriter(AnnotationOptsT o = ANNOTATE_SLICE,
                                   LLVMPointerAnalysis *pta = nullptr,
                                   LLVMDataDependenceAnalysis *dda = nullptr,
                                   const std::set<LLVMNode *>* criteria = nullptr)
        : opts(o), PTA(pta), DDA(dda), criteria(criteria)
    {
        assert(!(opts & ANNOTATE_PTR) || PTA);
        assert(!(opts & ANNOTATE_DU) || DDA);
    }

    void emitModuleComment(const std::string& comment) {
        module_comment = comment;
    }

    void emitModuleComment(std::string&& comment) {
        module_comment = std::move(comment);
    }

    void emitFunctionAnnot (const llvm::Function *,
                            llvm::formatted_raw_ostream &os) override
    {
        // dump the slicer's setting to the file
        // for easier comprehension
        static bool didit = false;
        if (!didit) {
            didit = true;
            os << module_comment;
        }
    }

    void emitInstructionAnnot(const llvm::Instruction *I,
                              llvm::formatted_raw_ostream& os) override
    {
        if (opts == 0)
            return;

        LLVMNode *node = nullptr;
        for (auto& it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            node = sub->getNode(const_cast<llvm::Instruction *>(I));
            if (node)
                break;
        }

        if (!node)
            return;

        emitNodeAnnotations(node, os);
    }

    void emitBasicBlockStartAnnot(const llvm::BasicBlock *B,
                                  llvm::formatted_raw_ostream& os) override
    {
        if (opts == 0)
            return;

        for (auto& it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            auto& cb = sub->getBlocks();
            auto I = cb.find(const_cast<llvm::BasicBlock *>(B));
            if (I != cb.end()) {
                LLVMBBlock *BB = I->second;
                if (opts & (ANNOTATE_POSTDOM | ANNOTATE_CD))
                    os << "  ; BB: " << BB << "\n";

                if (opts & ANNOTATE_POSTDOM) {
                    for (LLVMBBlock *p : BB->getPostDomFrontiers())
                        os << "  ; PDF: " << p << "\n";

                    LLVMBBlock *P = BB->getIPostDom();
                    if (P && P->getKey())
                        os << "  ; iPD: " << P << "\n";
                }

                if (opts & ANNOTATE_CD) {
                    for (LLVMBBlock *p : BB->controlDependence())
                        os << "  ; CD: " << p << "\n";
                }
            }
        }
    }
};

} // namespace debug
} // namespace dg

// allow combinations of annotation options
inline dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT
operator |(dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT a,
           dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT b) {

  using AnT = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;
  using T = std::underlying_type<AnT>::type;
  return static_cast<AnT>(static_cast<T>(a) | static_cast<T>(b));
}

inline dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT
operator |=(dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT& a,
           dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT b) {

  using AnT = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;
  using T = std::underlying_type<AnT>::type;
  a = static_cast<AnT>(static_cast<T>(a) | static_cast<T>(b));
  return a;
}

#endif // _LLVM_DG_ANNOTATION_WRITER_H_

