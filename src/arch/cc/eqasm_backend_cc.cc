/**
 * @file   eqasm_backend_cc.cc
 * @date   201809xx
 * @author Wouter Vlothuizen (wouter.vlothuizen@tno.nl)
 * @brief  eqasm backend for the Central Controller
 * @remark based on cc_light_eqasm_compiler.h, commit f34c0d9
 */

/*
    Change log:
    20200116:
    - changed JSON field "signal_ref" to "ref_signal" to improve consistency
    - idem "ref_signals_type" to "signal_type"

    Todo:
    - finish support for classical instructions
    - finish support for kernel conditionality
    - allow runtime selection of scheduler
    - port https://github.com/QE-Lab/OpenQL/pull/238 to CC
*/


// options:
#define OPT_CC_SCHEDULE_KERNEL_H    0       // 1=use scheduler from kernel.h iso cclight, overrides next option
#define OPT_CC_SCHEDULE_RC          0       // 1=use resource constraint scheduler

#include "eqasm_backend_cc.h"
#include "codegen_cc.h"

#include <options.h>
#include <platform.h>
#include <ir.h>
#include <circuit.h>
#include <scheduler.h>
// Including cc_light_resource_manager.h was of no use, hence it was deleted.
// #include <arch/cc_light/cc_light_resource_manager.h>
//
// Including cc_light_scheduler.h caused duplicate function definitions during make;
// instead of including it with its function bodies, declare those functions here and now ...
// WIP to create cc_schedule[_rc] after having solved scheduler's dependence on cc_light (HvS)
// #include <arch/cc_light/cc_light_scheduler.h>
namespace ql
{
    namespace arch
    {
#if OPT_CC_SCHEDULE_RC
        ql::ir::bundles_t cc_light_schedule_rc(ql::circuit & ckt,
            const ql::quantum_platform & platform, std::string & dot, size_t nqubits, size_t ncreg = 0);
#else
        ql::ir::bundles_t cc_light_schedule(ql::circuit & ckt,
            const ql::quantum_platform & platform, std::string & dot, size_t nqubits, size_t ncreg = 0);
#endif

    } // end of namespace arch
} // end of namespace ql


// define classical QASM instructions as generated by classical.h
// FIXME: should be moved to a more sensible location
#define QASM_CLASSICAL_INSTRUCTION_LIST   \
    X(QASM_ADD, "add") \
    X(QASM_SUB, "sub") \
    X(QASM_AND, "and") \
    X(QASM_OR, "or")   \
    X(QASM_XOR, "xor") \
    X(QASM_NOT, "not") \
    X(QASM_NOP, "nop") \
    X(QASM_LDI, "ldi") \
    X(QASM_MOV, "mov") \
    X(QASM_EQ, "eq")   \
    X(QASM_NE, "ne")   \
    X(QASM_LT, "lt")   \
    X(QASM_GT, "gt")   \
    X(QASM_LE, "le")   \
    X(QASM_GE, "ge")

#if 0   // FIXME
// generate enum for instructions
#define X(_enum, _string) _enum
enum eQASM {
    QASM_CLASSICAL_INSTRUCTION_LIST
};
#undef X
#endif


// generate constants for instructions
#define X(_enum, _string) static const char *_enum = _string;
QASM_CLASSICAL_INSTRUCTION_LIST
#undef X




namespace ql
{
namespace arch
{

// compile for Central Controller
// NB: a new eqasm_backend_cc is instantiated per call to compile, so we don't need to cleanup
void eqasm_backend_cc::compile(std::string prog_name, std::vector<quantum_kernel> kernels, const ql::quantum_platform &platform)
{
#if 1   // FIXME: patch for issue #164, should be moved to caller
    if(kernels.size() == 0) {
        FATAL("Trying to compile empty kernel");
    }
#endif
    DOUT("Compiling " << kernels.size() << " kernels to generate Central Controller program ... ");

    // init
    load_hw_settings(platform);
    codegen.init(platform);
    bundleIdx = 0;

    // generate program header
    codegen.program_start(prog_name);

    // generate code for all kernels
    for(auto &kernel : kernels) {
        IOUT("Compiling kernel: " << kernel.name);
        codegen_kernel_prologue(kernel);

        ql::circuit& ckt = kernel.c;
        if (!ckt.empty()) {
#if OPT_CC_SCHEDULE_KERNEL_H    // FIXME: WIP
            // FIXME: try kernel.h::schedule()
            std::string kernel_sched_qasm;
            std::string kernel_sched_dot;
            std::string kernel_dot;
            kernel.schedule(platform, kernel_sched_qasm, kernel_dot, kernel_sched_dot);
            ql::ir::bundles_t bundles = ql::ir::bundler(kernel.c, platform.cycle_time);
#else
            auto creg_count = kernel.creg_count;     // FIXME: there is no platform.creg_count

#if OPT_CC_SCHEDULE_RC
            // schedule with platform resource constraints
            std::string     sched_dot;
            ql::ir::bundles_t bundles = cc_light_schedule_rc(ckt, platform, sched_dot, platform.qubit_number, creg_count);
#else
            // schedule without resource constraints
            /* FIXME: we use the "CC-light" scheduler, which actually has little platform specifics apart from
             * requiring us to define a field "cc_light_instr" for every instruction in the JSON configuration file.
             * That function could and should be generalized.
             */
            std::string     sched_dot;
            ql::ir::bundles_t bundles = cc_light_schedule(ckt, platform, sched_dot, platform.qubit_number, creg_count);
#endif
#endif


#if 0   // FIXME: from CClight, where it is called from the 'circuit' compile() function, i.e. never in practice
            // write RC scheduled bundles with parallelism as simple QASM file
            // FIXME: writes only single kernel, ah well, overwrites file for every kernel
            std::stringstream sched_qasm;
            sched_qasm << "qubits " << platform.qubit_number << "\n\n"
                       << ".fused_kernels";
            string fname( ql::options::get("output_dir") + "/" + prog_name + "_scheduled_rc.qasm");
            IOUT("Writing Resource-contraint scheduled QASM to " << fname);
            sched_qasm << ql::ir::qasm(bundles);
            ql::utils::write_file(fname, sched_qasm.str());
#endif

            codegen.kernel_start();
            codegen_bundles(bundles, platform);
            codegen.kernel_finish(kernel.name, bundles.back().start_cycle+bundles.back().duration_in_cycles);
        } else {
            DOUT("Empty kernel: " << kernel.name);                      // NB: normal situation for kernels with classical control
        }

        codegen_kernel_epilogue(kernel);
    }

    codegen.program_finish(prog_name);

    // write program to file
    std::string file_name(ql::options::get("output_dir") + "/" + prog_name + ".vq1asm");
    IOUT("Writing Central Controller program to " << file_name);
    ql::utils::write_file(file_name, codegen.getCode());

    // write instrument map to file (unless we were using input file)
    std::string map_input_file = ql::options::get("backend_cc_map_input_file");
    if(map_input_file != "") {
        std::string file_name_map(ql::options::get("output_dir") + "/" + prog_name + ".map");
        IOUT("Writing instrument map to " << file_name_map);
        ql::utils::write_file(file_name_map, codegen.getMap());
    }

    DOUT("Compiling Central Controller program [Done]");
}


void eqasm_backend_cc::compile(std::string prog_name, ql::circuit &ckt, ql::quantum_platform &platform)
{
    FATAL("Circuit compilation not implemented, because it does not support classical kernel operations");
}

// based on cc_light_eqasm_compiler.h::classical_instruction2qisa/decompose_instructions
// NB: input instructions defined in classical.h::classical
void eqasm_backend_cc::codegen_classical_instruction(ql::gate *classical_ins)
{
    auto &iname =  classical_ins->name;
    auto &iopers = classical_ins->creg_operands;
    int iopers_count = iopers.size();

    if(  iname == QASM_ADD || iname == QASM_SUB ||
         iname == QASM_AND || iname == QASM_OR  || iname == QASM_NOT || iname == QASM_XOR ||
         iname == QASM_LDI || iname == QASM_MOV ||
         iname == QASM_NOP
      )
    {
        FATAL("Classical instruction not implemented: " << iname);
    }
    else if( iname == QASM_EQ || iname == QASM_NE || iname == QASM_LT ||
             iname == QASM_GT || iname == QASM_LE || iname == QASM_GE
           )
    {
        FATAL("Classical instruction not implemented: " << iname);
    }
    else
    {
        FATAL("Unknown classical operation'" << iname << "' with'" << iopers_count << "' operands!");
    }
}


// get label from kernel name: FIXME: the label is the program name
// FIXME: the kernel name has a structure (e.g. "sp1_for1_start" or "sp1_for1_start") which we use here. This should be made explicit
// FIXME: looks very inefficient
// extracted from get_epilogue
// FIXME: does not use class
std::string eqasm_backend_cc::kernelLabel(ql::quantum_kernel &k)
{
    std::string kname(k.name);
    std::replace(kname.begin(), kname.end(), '_', ' ');
    std::istringstream iss(kname);
    std::vector<std::string> tokens{ std::istream_iterator<std::string>{iss},
                                     std::istream_iterator<std::string>{} };
    return tokens[0];
}

// handle kernel conditionality at beginning of kernel
// based on cc_light_eqasm_compiler.h::get_prologue
void eqasm_backend_cc::codegen_kernel_prologue(ql::quantum_kernel &k)
{
    codegen.comment(SS2S("### Kernel: '" << k.name << "'"));

    // FIXME: insert waits to compensate latencies.

    switch(k.type) {
        case kernel_type_t::IF_START:
        {
            auto op0 = k.br_condition.operands[0]->id;
            auto op1 = k.br_condition.operands[1]->id;
            auto opName = k.br_condition.operation_name;
            codegen.if_start(op0, opName, op1);
            break;
        }

        case kernel_type_t::ELSE_START:
        {
            auto op0 = k.br_condition.operands[0]->id;
            auto op1 = k.br_condition.operands[1]->id;
            auto opName = k.br_condition.operation_name;
            codegen.else_start(op0, opName, op1);
            break;
        }

        case kernel_type_t::FOR_START:
        {
            std::string label = kernelLabel(k);
            codegen.for_start(label, k.iterations);
            break;
        }

        case kernel_type_t::DO_WHILE_START:
        {
            std::string label = kernelLabel(k);
            codegen.do_while_start(label);
            break;
        }

        case kernel_type_t::STATIC:
        case kernel_type_t::FOR_END:
        case kernel_type_t::DO_WHILE_END:
        case kernel_type_t::IF_END:
        case kernel_type_t::ELSE_END:
            // do nothing
            break;

        default:
            FATAL("inconsistency detected: unhandled kernel type");
            break;
    }
}


// handle kernel conditionality at end of kernel
// based on cc_light_eqasm_compiler.h::get_epilogue
void eqasm_backend_cc::codegen_kernel_epilogue(ql::quantum_kernel &k)
{
    // FIXME: insert waits to align kernel duration (in presence of latency compensation)

    switch(k.type) {
        case kernel_type_t::FOR_END:
        {
            std::string label = kernelLabel(k);
            codegen.for_end(label);
            break;
        }

        case kernel_type_t::DO_WHILE_END:
        {
            auto op0 = k.br_condition.operands[0]->id;
            auto op1 = k.br_condition.operands[1]->id;
            auto opName = k.br_condition.operation_name;
            std::string label = kernelLabel(k);
            codegen.do_while_end(label, op0, opName, op1);
            break;
        }

        case kernel_type_t::IF_END:
            // do nothing? FIXME
            break;

        case kernel_type_t::ELSE_END:
            // do nothing? FIXME
            break;

        case kernel_type_t::STATIC:
        case kernel_type_t::IF_START:
        case kernel_type_t::ELSE_START:
        case kernel_type_t::FOR_START:
        case kernel_type_t::DO_WHILE_START:
            // do nothing
            break;

        default:
            FATAL("inconsistency detected: unhandled kernel type");
            break;
    }
}


// based on cc_light_eqasm_compiler.h::bundles2qisa()
void eqasm_backend_cc::codegen_bundles(ql::ir::bundles_t &bundles, const ql::quantum_platform &platform)
{
    IOUT("Generating .vq1asm for bundles");

    for(ql::ir::bundle_t &bundle : bundles) {
        // generate bundle header
        codegen.bundle_start(SS2S("## Bundle " << bundleIdx++
                                  << ": start_cycle=" << bundle.start_cycle
                                  << ", duration_in_cycles=" << bundle.duration_in_cycles << ":"
                                  ));
        // NB: the "wait" instruction never makes it into the bundle. It is accounted for in scheduling though,
        // and if a non-zero duration is specified that duration is reflected in 'start_cycle' of the subsequent instruction

        // generate code for this bundle
        for(auto section = bundle.parallel_sections.begin(); section != bundle.parallel_sections.end(); ++section ) {
            // check whether section defines classical gate
            ql::gate *firstInstr = *section->begin();
            auto firstInstrType = firstInstr->type();
            if(firstInstrType == __classical_gate__) {
                if(section->size() != 1) {
                    FATAL("Inconsistency detected in bundle contents: classical gate with parallel sections");
                }
                codegen_classical_instruction(firstInstr);
            } else {
                /* iterate over all instructions in section.
                 * NB: our strategy differs from cc_light_eqasm_compiler, we have no special treatment of first instruction
                 * and don't require all instructions to be identical
                 */
                for(auto insIt = section->begin(); insIt != section->end(); ++insIt) {
                    ql::gate *instr = *insIt;
                    ql::gate_type_t itype = instr->type();
                    std::string iname = instr->name;

                    switch(itype) {
                        case __nop_gate__:       // a quantum "nop", see gate.h
                            codegen.nop_gate();
                            break;

                        case __classical_gate__:
                            FATAL("Inconsistency detected in bundle contents: classical gate found after first section (which itself was non-classical)");
                            break;

                        case __custom_gate__:
                            codegen.custom_gate(iname, instr->operands, instr->creg_operands, instr->angle, bundle.start_cycle, instr->duration);
                            break;

                        case __display__:
                            FATAL("Gate type __display__ not supported");           // QX specific, according to openql.pdf
                            break;

                        case __measure_gate__:
                            FATAL("Gate type __measure_gate__ not supported");      // no use, because there is no way to define CC-specifics
                            break;

                        default:
                            FATAL("Unsupported gate type: " << itype);
                    }   // switch(itype)
                } // for(section...)
            }
        }

        // generate bundle trailer, and code for classical gates
        bool isLastBundle = &bundle==&bundles.back();
        codegen.bundle_finish(bundle.start_cycle, bundle.duration_in_cycles, isLastBundle);
    }   // for(bundles)

    IOUT("Generating .vq1asm for bundles [Done]");
}


// based on: cc_light_eqasm_compiler.h::load_hw_settings
void eqasm_backend_cc::load_hw_settings(const ql::quantum_platform &platform)
{
#if 0   // FIXME: currently unused, may be of future use
    const struct {
        size_t  *var;
        std::string name;
    } hw_settings[] = {
#if 0   // FIXME: Convert to cycle. // NB: Visual Studio does not like empty array
        { &mw_mw_buffer,            "mw_mw_buffer" },
        { &mw_flux_buffer,          "mw_flux_buffer" },
        { &mw_readout_buffer,       "mw_readout_buffer" },
        { &flux_mw_buffer,          "flux_mw_buffer" },
        { &flux_flux_buffer,        "flux_flux_buffer" },
        { &flux_readout_buffer,     "flux_readout_buffer" },
        { &readout_mw_buffer,       "readout_mw_buffer" },
        { &readout_flux_buffer,     "readout_flux_buffer" },
        { &readout_readout_buffer,  "readout_readout_buffer" }
#endif
    };

    DOUT("Loading hardware settings ...");
    size_t i=0;
    try
    {
        for(i=0; i<ELEM_CNT(hw_settings); i++) {
            size_t val = platform.hardware_settings[hw_settings[i].name];
            *hw_settings[i].var = val;
        }
    }
    catch (json::exception &e)
    {
        throw ql::exception(
            "[x] error : ql::eqasm_compiler::compile() : error while reading hardware settings : parameter '"
            + hw_settings[i].name
            + "'\n\t"
            + std::string(e.what()), false);
    }
#endif
}

} // arch
} // ql

