/*
 * Copyright 2011 Troels Blum <troels@blum.dk>
 *
 * This file is part of cphVB.
 *
 * cphVB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cphVB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cphVB.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __INSTRUCTIONSCHEDULERSIMPLE_HPP
#define __INSTRUCTIONSCHEDULERSIMPLE_HPP

#include <map>
#include "cphVBinstruction.hpp"
#include "InstructionScheduler.hpp"
#include "InstructionBatchSimple.hpp"
#include "KernelGenerator.hpp"
#include "DataManager.hpp"

typedef std::map<Threads, InstructionBatch*> BatchTable;

class InstructionSchedulerSimple : public InstructionScheduler
{
private:
    DataManager* dataManager;
    KernelGenerator* kernelGenerator;
    BatchTable batchTable;
public:
    InstructionSchedulerSimple(DataManager* dataManager,
                               KernelGenerator* kernelGenerator);
    void schedule(cphVBinstruction* inst);
    void schedule(cphvb_intp instructionCount,
                  cphVBinstruction* instructionList);
    void flush();
};

#endif
