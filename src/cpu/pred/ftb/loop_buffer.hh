#ifndef __CPU_PRED_FTB_LOOP_BUFFER_HH__
#define __CPU_PRED_FTB_LOOP_BUFFER_HH__

#include <array>
#include <queue>
#include <stack>
#include <utility>
#include <vector>

#include "cpu/pred/bpred_unit.hh"
#include "cpu/pred/ftb/stream_struct.hh"
#include "debug/LoopBuffer.hh"


namespace gem5
{

namespace branch_prediction
{

namespace ftb_pred
{

class LoopBuffer
{
  public:
    /** The loop unrolled buffer. */
    uint8_t activeBuffer[256];

    uint8_t *activePointer;

    // filled at fetch time
    typedef struct InstDesc {
        StaticInstPtr inst;
        bool compressed;
        Addr pc;
    } InstDesc;

    InstDesc genInstDesc(bool compressed, StaticInstPtr inst, Addr pc) {
        InstDesc desc;
        desc.compressed = compressed;
        desc.inst = inst;
        desc.pc = pc;
        return desc;
    }
    std::map<Addr, std::vector<InstDesc>> specLoopInsts;
    
    
    // used in loop
    std::pair<Addr, std::vector<InstDesc>> loopInsts;
    Addr loopBranchPC;
    int loopInstCounter{0};

    // store fetch stream infos of entry before entering loop
    FetchStream streamBeforeLoop;


    /** Find a loop entry and update the active loop entry.
     * Unroll it until loop exit.
     */

    bool tryActivateLoop(Addr start_pc) {
        /**
         * @todo: caculation limit, singleIterSize
         * 
         */
        DPRINTF(LoopBuffer, "query loop buffer with start pc %#lx\n", start_pc);
        if (loopInsts.first == start_pc && streamBeforeLoop.predTaken) {
            DPRINTF(LoopBuffer, "loop buffer activated\n");
            active = true;
            return true;
        }
        return false;
    }

    void recordNewestStreamOutsideLoop(FetchStream stream) { streamBeforeLoop = stream; }

    unsigned limit;

    unsigned singleIterSize;

    bool active{false};

    /** check current offset against unrolled words
     * For example, #iteration = 5, each iteration has 10 bytes (2NC + 1C)
     * Then limit = 5 * 10 bytes = 5 * 10 / 4 words.
     * When offset == 12, all payloads in loop buffer is used up. 2 byte
     * dummy payloads should be provided in activeBuffer.
     * 
     * Another example, #iteration = 5, each iteration has 12 bytes (3NC)
     * Then limit = 5 * 12 bytes = 5 * 12 / 4 words.
     * When offset == 15, all payloads in loop buffer is used up
     * 
     * return true if used up
     */
    bool notifyOffset(unsigned offset)
    {
        if (offset == singleIterSize) {
            // move activePointer to activeBuffer to simulate loop unrolling
            activePointer = activeBuffer;
        }
        if (offset == limit) {
            active = false;
            return true;
        }
        return false;
    }

    void deactivate(bool squash)
    {
        // activePointer = nullptr;
        active = false;
        if (squash) {
            loopInstCounter = 0;
        }
        // limit = 0;
        // singleIterSize = 0;
        DPRINTF(LoopBuffer, "deactivating loop buffer\n");
    }

    bool isActive() { return active; }

    Addr getActiveLoopStart() { return loopInsts.first; }

    int getActiveLoopInstsSize() { return loopInsts.second.size(); }

    Addr getActiveLoopBranch() { return loopBranchPC; }

    bool activeLoopMayBeDouble() { return getActiveLoopInstsSize() <= 16 / 2; }

    // called at fetchQueue enqueue, after a full ftq entry is enqueued,
    // and the entry is ended by a backward taken branch
    bool fillSpecLoopBuffer(Addr pc, const std::vector<InstDesc> &insts)
    {
        // TODO: use replacement policy
        const auto &it = specLoopInsts.find(pc);
        if (it != specLoopInsts.end()) {
            DPRINTF(LoopBuffer, "found spec loop buffer entry for pc %#lx, don't fill, entry has %d insts\n",
                pc, it->second.size());
            return false;
        } else {
            specLoopInsts[pc] = insts;
            return true;
        }
    }

    void commitLoopPeek(Addr pc, Addr branch_pc) {
        const auto &it = specLoopInsts.find(pc);
        if (it != specLoopInsts.end()) {
            loopInsts.first = pc;
            loopInsts.second = it->second;
            loopBranchPC = branch_pc;
            DPRINTF(LoopBuffer, "found spec loop buffer entry for pc %#lx, entry has %d insts\n",
                pc, loopInsts.second.size());
            // specLoopInsts.erase(it);
        }
    }

    InstDesc supplyInst() {
        DPRINTF(LoopBuffer, "supplying inst from loop buffer, loopInstCounter %d, buffer size %d\n",
            loopInstCounter, loopInsts.second.size());
        if (loopInstCounter < loopInsts.second.size()) {
            auto instDesc = loopInsts.second[loopInstCounter];
            if (++loopInstCounter >= loopInsts.second.size()) {
                loopInstCounter = 0;
            }
            return instDesc;
        } else {
            assert(false);
            return InstDesc{nullptr, false, 0};
        }
    }

    int getLoopInstNum(Addr pc) {
        if (loopInsts.first == pc) {
            return loopInsts.second.size();
        } else {
            // FIXME: record in fsq entry
            return 0;
        }
    }

    void clearState() {
        loopInstCounter = 0;
    }
};
}  // namespace ftb_pred
}  // namespace branch_prediction
}  // namespace gem5

#endif  // __CPU_PRED_FTB_LOOP_BUFFER_HH__