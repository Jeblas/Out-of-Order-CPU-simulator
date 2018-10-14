/***********************************************************************
 * File         : pipeline.cpp
 * Author       : Moinuddin K. Qureshi
 * Date         : 19th February 2014
 * Description  : Out of Order Pipeline for Lab3 ECE 6100
 **********************************************************************/

#include "pipeline.h"
#include <cstdlib>
#include <cstring>
#include <vector>


extern int32_t PIPE_WIDTH;
extern int32_t SCHED_POLICY;
extern int32_t LOAD_EXE_CYCLES;

/**********************************************************************
 * Support Function: Read 1 Trace Record From File and populate Fetch Inst
 **********************************************************************/

void pipe_fetch_inst(Pipeline *p, Pipe_Latch* fe_latch){
    static int halt_fetch = 0;
    uint8_t bytes_read = 0;
    Trace_Rec trace;
    if(halt_fetch != 1) {
      bytes_read = fread(&trace, 1, sizeof(Trace_Rec), p->tr_file);
      Inst_Info *fetch_inst = &(fe_latch->inst);
    // check for end of trace
    // Send out a dummy terminate op
      if( bytes_read < sizeof(Trace_Rec)) {
        p->halt_inst_num=p->inst_num_tracker;
        halt_fetch = 1;
        fe_latch->valid=true;
        fe_latch->inst.dest_reg = -1;
        fe_latch->inst.src1_reg = -1;
        fe_latch->inst.src1_reg = -1;
        fe_latch->inst.inst_num=-1;
        fe_latch->inst.op_type=4;
        return;
      }

    // got an instruction ... hooray!
      fe_latch->valid=true;
      fe_latch->stall=false;
      p->inst_num_tracker++;
      fetch_inst->inst_num=p->inst_num_tracker;
      fetch_inst->op_type=trace.op_type;

      fetch_inst->dest_reg=trace.dest_needed? trace.dest:-1;
      fetch_inst->src1_reg=trace.src1_needed? trace.src1_reg:-1;
      fetch_inst->src2_reg=trace.src2_needed? trace.src2_reg:-1;

      fetch_inst->dr_tag=-1;
      fetch_inst->src1_tag=-1;
      fetch_inst->src2_tag=-1;
      fetch_inst->src1_ready=false;
      fetch_inst->src2_ready=false;
      fetch_inst->exe_wait_cycles=0;
    } else {
      fe_latch->valid = false;
    }
    return; 
}


/**********************************************************************
 * Pipeline Class Member Functions 
 **********************************************************************/

Pipeline * pipe_init(FILE *tr_file_in){
    printf("\n** PIPELINE IS %d WIDE **\n\n", PIPE_WIDTH);

    // Initialize Pipeline Internals
    Pipeline *p = (Pipeline *) calloc (1, sizeof (Pipeline));
    
    p->pipe_RAT=RAT_init();
    p->pipe_ROB=ROB_init();
    p->pipe_REST=REST_init();
    p->pipe_EXEQ=EXEQ_init();
    p->tr_file = tr_file_in;
    p->halt_inst_num = ((uint64_t)-1) - 3;           
    int ii =0;
    for(ii = 0; ii < PIPE_WIDTH; ii++) {  // Loop over No of Pipes
      p->FE_latch[ii].valid = false;
      p->ID_latch[ii].valid = false;
      p->EX_latch[ii].valid = false;
      p->SC_latch[ii].valid = false;
    } 
    return p;
}


/**********************************************************************
 * Print the pipeline state (useful for debugging)
 **********************************************************************/

void pipe_print_state(Pipeline *p){
    std::cout << "--------------------------------------------" << std::endl;
    std::cout <<"cycle count : " << p->stat_num_cycle << " retired_instruction : " << p->stat_retired_inst << std::endl;
    uint8_t latch_type_i = 0;
    uint8_t width_i      = 0;
   for(latch_type_i = 0; latch_type_i < 4; latch_type_i++) {
        switch(latch_type_i) {
        case 0:
            printf(" FE: ");
            break;
        case 1:
            printf(" ID: ");
            break;
        case 2:
            printf(" SCH: ");
            break;
        case 3:
            printf(" EX: ");
            break;
        default:
            printf(" -- ");
          }
    }
   printf("\n");
   for(width_i = 0; width_i < PIPE_WIDTH; width_i++) {
       if(p->FE_latch[width_i].valid == true) {
         printf("  %d  ", (int)p->FE_latch[width_i].inst.inst_num);
       } else {
         printf(" --  ");
       }
       if(p->ID_latch[width_i].valid == true) {
         printf("  %d  ", (int)p->ID_latch[width_i].inst.inst_num);
       } else {
         printf(" --  ");
       }
       if(p->SC_latch[width_i].valid == true) {
         printf("  %d  ", (int)p->SC_latch[width_i].inst.inst_num);
       } else {
         printf(" --  ");
       }
       if(p->EX_latch[width_i].valid == true) {
         for(int ii = 0; ii < MAX_BROADCASTS; ii++) {
            if(p->EX_latch[ii].valid)
	      printf("  %d  ", (int)p->EX_latch[ii].inst.inst_num);
         }  
       } else {
         printf(" --  ");
       }
        printf("\n");
     }
     printf("\n");
      
     RAT_print_state(p->pipe_RAT);
     REST_print_state(p->pipe_REST);
     EXEQ_print_state(p->pipe_EXEQ);
     ROB_print_state(p->pipe_ROB);
}


/**********************************************************************
 * Pipeline Main Function: Every cycle, cycle the stage 
 **********************************************************************/

void pipe_cycle(Pipeline *p)
{
    p->stat_num_cycle++;

    pipe_cycle_commit(p);
      //pipe_print_state(p);
    pipe_cycle_broadcast(p);
      //pipe_print_state(p);
    pipe_cycle_exe(p);
      //pipe_print_state(p);
    pipe_cycle_schedule(p);
      //pipe_print_state(p);
    pipe_cycle_rename(p);
      //pipe_print_state(p);
    pipe_cycle_decode(p);
      //pipe_print_state(p);
    pipe_cycle_fetch(p);
      //pipe_print_state(p);
    //std::cin.ignore();
    //if (p->stat_num_cycle >= 104955) {
    //  pipe_print_state(p);
    //  std::cin.ignore();
    //}
}

//--------------------------------------------------------------------//

void pipe_cycle_fetch(Pipeline *p){
  int ii = 0;
  Pipe_Latch fetch_latch;

  for(ii=0; ii<PIPE_WIDTH; ii++) {
    if((p->FE_latch[ii].stall) || (p->FE_latch[ii].valid)) {   // Stall 
        continue;

    } else {  // No Stall and Latch Empty
        pipe_fetch_inst(p, &fetch_latch);
        // copy the op in FE LATCH
        p->FE_latch[ii]=fetch_latch;
    }
  }
}

//--------------------------------------------------------------------//

void pipe_cycle_decode(Pipeline *p){
   int ii = 0;

   int jj = 0;

   static uint64_t start_inst_id = 1;

   // Loop Over ID Latch
   for(ii=0; ii<PIPE_WIDTH; ii++){ 
     if((p->ID_latch[ii].stall == 1) || (p->ID_latch[ii].valid)) { // Stall
       continue;  
     } else {  // No Stall & there is Space in Latch
       for(jj = 0; jj < PIPE_WIDTH; jj++) { // Loop Over FE Latch
	      if(p->FE_latch[jj].valid) {
	        if(p->FE_latch[jj].inst.inst_num == start_inst_id) { // In Order Inst Found
	          p->ID_latch[ii]        = p->FE_latch[jj];
	          p->ID_latch[ii].valid  = true;
	          p->FE_latch[jj].valid  = false;
	          start_inst_id++;
	          break;
	        }      
	      }
      }
    }
   }
   
}

//--------------------------------------------------------------------//

void pipe_cycle_exe(Pipeline *p){

  int ii;
  //If all operations are single cycle, simply copy SC latches to EX latches
  if(LOAD_EXE_CYCLES == 1) {
    for(ii=0; ii<PIPE_WIDTH; ii++){
      if(p->SC_latch[ii].valid) {
        p->EX_latch[ii]=p->SC_latch[ii];
        p->EX_latch[ii].valid = true;
        p->SC_latch[ii].valid = false; 
      }
    }
    return;
  }
  
  //---------Handling exe for multicycle operations is complex, and uses EXEQ
  
  // All valid entries from SC get into exeq  
  
  for(ii = 0; ii < PIPE_WIDTH; ii++) {
    if(p->SC_latch[ii].valid) {
      EXEQ_insert(p->pipe_EXEQ, p->SC_latch[ii].inst);
      p->SC_latch[ii].valid = false;
    }
  }
  
  // Cycle the exeq, to reduce wait time for each inst by 1 cycle
  EXEQ_cycle(p->pipe_EXEQ);
  
  // Transfer all finished entries from EXEQ to EX_latch
  int index = 0;
  
  while(1) {
    if(EXEQ_check_done(p->pipe_EXEQ)) {
      p->EX_latch[index].valid = true;
      p->EX_latch[index].stall = false;
      p->EX_latch[index].inst  = EXEQ_remove(p->pipe_EXEQ);
      index++;
    } else { // No More Entry in EXEQ
      break;
    }
  }
}



/**********************************************************************
 * -----------  DO NOT MODIFY THE CODE ABOVE THIS LINE ----------------
 **********************************************************************/

void pipe_cycle_rename(Pipeline *p){

  // TODO: If src1/src2 is remapped set src1tag, src2tag
  // TODO: Find space in ROB and set drtag as such if successful
  // TODO: Find space in REST and transfer this inst (valid=1, sched=0)
  // TODO: If src1/src2 is not remapped marked as src ready
  // TODO: If src1/src2 remapped and the ROB (tag) is ready then mark src ready
  // FIXME: If there is stall, we should not do rename and ROB alloc twice
  // Check ID latch
  // Hack to get in order inserts into rob
  if (PIPE_WIDTH == 2 && p->ID_latch[0].inst.inst_num > p->ID_latch[1].inst.inst_num) {
    Pipe_Latch temp = p->ID_latch[0];
    p->ID_latch[0] = p->ID_latch[1];
    p->ID_latch[1] = temp;
  }
  for(int ii=0; ii<PIPE_WIDTH; ii++){ 
      //Check if ROB and REST have space
      // TODO check for stall
    if (p->ID_latch[ii].valid) {
      //  continue;   
      if (ROB_check_space(p->pipe_ROB) && REST_check_space(p->pipe_REST)) {
        Inst_Info instruction = p->ID_latch[ii].inst;
        //Place instr in rob
        int drtag = ROB_insert(p->pipe_ROB, instruction);
        instruction.dr_tag = drtag;

        if (instruction.src1_reg != -1) {
         //Check RAT for renamed
          int remap = RAT_get_remap(p->pipe_RAT, instruction.src1_reg);
          if (remap == -1) {
            // Register is ready
            instruction.src1_ready = true;
            //instruction.src1_tag = -1;
          } else {
            // Asssign register remap and check ready
            instruction.src1_ready = ROB_check_ready(p->pipe_ROB, remap);
            instruction.src1_tag = remap;
          }
        } else {
          instruction.src1_ready = true;
        }

        if (instruction.src2_reg != -1) {
         //Check RAT for renamed
          int remap = RAT_get_remap(p->pipe_RAT, instruction.src2_reg);
          if (remap == -1) {
            // Register is ready
            instruction.src2_ready = true;
            instruction.src2_tag = -1;
          } else {
            // Asssign register remap and check ready
            instruction.src2_ready = ROB_check_ready(p->pipe_ROB, remap);
            instruction.src2_tag = remap;
          }
        } else {
          instruction.src2_ready = true;
        }
        // Add instruction to REST
        REST_insert(p->pipe_REST,instruction);
        // Add drtag to to RAT
        //std::cout << "RAT arf_ID : " << instruction.dest_reg <<std::endl;
        if (instruction.dest_reg != -1) {
          RAT_set_remap(p->pipe_RAT, instruction.dest_reg, drtag);
          //std::cout << "DR_TAGE" << drtag << std::endl;
          //std::cout << "RAT arf_ID : " << instruction.dest_reg <<std::endl;
        }
        p->ID_latch[ii].valid = false;
      } else {
        // stall no space in ROB or REST
      }
      
    } else {
      // stall
    }
  }
}

//--------------------------------------------------------------------//

void pipe_cycle_schedule(Pipeline *p){

  // TODO: Implement two scheduling policies (SCHED_POLICY: 0 and 1)

  if(SCHED_POLICY==0){
    // inorder scheduling
    // Find all valid entries, if oldest is stalled then stop
    // Else send it out and mark it as scheduled
    for(int ii=0; ii<PIPE_WIDTH; ii++){
      // Get oldest valid entry
      REST_Entry oldest_entry;
      oldest_entry.valid = false;
      for (int jj = 0; jj < MAX_REST_ENTRIES; ++jj) {
        REST_Entry rest_entry = p->pipe_REST->REST_Entries[jj];
        // TODO perhaps the oldest instruction can be scheduled
        if (rest_entry.valid && !rest_entry.scheduled) {
          //rest_entry.inst.inst_num;
          if (oldest_entry.valid == false) {
            oldest_entry = rest_entry;
          } else {
            if (oldest_entry.inst.inst_num > rest_entry.inst.inst_num) {
              // have new oldest instruction
              oldest_entry = rest_entry;
            }
          }
        }
      } 
      //if (ii != 0 && p->SC_latch[ii - 1].stall == true) {
      //  p->SC_latch[ii].stall = true;
      //  p->SC_latch[ii].valid = false;
      //  continue;
      //}
      if (oldest_entry.inst.src1_ready && oldest_entry.inst.src2_ready && oldest_entry.valid) {
        p->SC_latch[ii].inst = oldest_entry.inst;
        p->SC_latch[ii].stall = false;
        p->SC_latch[ii].valid = oldest_entry.valid;
        // send out oldest
        REST_schedule(p->pipe_REST, oldest_entry.inst);
      } else {
        // oldest entry is stalled : STOP
        // possible check to see if 
        p->SC_latch[ii].stall = true;
        p->SC_latch[ii].valid = false;
      }
    }
  }

  if(SCHED_POLICY==1){
    // out of order scheduling
    // Find valid/unscheduled/src1ready/src2ready entries in REST
    // Transfer them to SC_latch and mark that REST entry as scheduled
  
    // FIND valid entries
    std::vector<REST_Entry> entries_to_sched;
    for (int ii = 0; ii < MAX_REST_ENTRIES; ++ii) {
      REST_Entry rest_entry = p->pipe_REST->REST_Entries[ii];
      if (rest_entry.valid && !rest_entry.scheduled && rest_entry.inst.src1_ready
          && rest_entry.inst.src2_ready) {
            entries_to_sched.push_back(rest_entry);
          }
    }

    // get the oldest entries in entries to schedule and schedule them
    for(int ii=0; ii<PIPE_WIDTH; ii++){ 
      REST_Entry oldest_entry;
      oldest_entry.valid = false;
      int oldest_entry_index = -1;
      for (int kk = 0; kk < entries_to_sched.size(); ++kk) {
        if (oldest_entry.valid == false) {
          oldest_entry = entries_to_sched[kk];
          oldest_entry_index = kk;
        } else {
          if (oldest_entry.inst.inst_num > entries_to_sched[kk].inst.inst_num) {
            oldest_entry = entries_to_sched[kk];
            oldest_entry_index = kk;
          }
        }
      }
      // Have oldest viable entry
      if (oldest_entry_index != -1) {
        p->SC_latch[ii].inst = oldest_entry.inst;
        p->SC_latch[ii].stall = false;
        p->SC_latch[ii].valid = oldest_entry.valid;
        // send out oldest
        REST_schedule(p->pipe_REST, oldest_entry.inst);
        entries_to_sched.erase(entries_to_sched.begin() + oldest_entry_index);
      } else {
        //Mark SC_Latch as invalid
        p->SC_latch[ii].stall = true;
        p->SC_latch[ii].valid = false;
      }
    }
  }
}


//--------------------------------------------------------------------//

void pipe_cycle_broadcast(Pipeline *p){

  // TODO: Go through all instructions out of EXE latch
  // TODO: Broadcast it to REST (using wakeup function)
  // TODO: Remove entry from REST (using inst_num)
  // TODO: Update the ROB, mark ready, and update Inst Info in ROB
  for(int ii = 0; ii < MAX_BROADCASTS; ++ii) {
      Pipe_Latch current_latch = p->EX_latch[ii];
      if (current_latch.valid && !current_latch.stall) {
        REST_wakeup(p->pipe_REST, current_latch.inst.dr_tag);
        REST_remove(p->pipe_REST, current_latch.inst);
        ROB_mark_ready(p->pipe_ROB, current_latch.inst);
        p->EX_latch[ii].valid = false;
      }

  }
 
}


//--------------------------------------------------------------------//


void pipe_cycle_commit(Pipeline *p) {
  //int ii = 0;

  // TODO: check the head of the ROB. If ready commit (update stats)
  // TODO: Deallocate entry from ROB
  // TODO: Update RAT after checking if the mapping is still valid
  //while (ROB_check_head(p->pipe_ROB)) {
  //  Inst_Info inst_to_commit = ROB_remove_head(p->pipe_ROB);
  //  if (RAT_get_remap(p->pipe_RAT, inst_to_commit.dest_reg) == inst_to_commit.dr_tag) {
  //    RAT_reset_entry(p->pipe_RAT, inst_to_commit.dest_reg);
  //  }
    //++(p->stat_retired_inst);
  //}
  for(int jj=0; jj<PIPE_WIDTH; jj++){ 
    if (ROB_check_head(p->pipe_ROB)) {
      // TODO never reach here
      Inst_Info inst_to_commit = ROB_remove_head(p->pipe_ROB);
      if (RAT_get_remap(p->pipe_RAT, inst_to_commit.dest_reg) == inst_to_commit.dr_tag) {
        RAT_reset_entry(p->pipe_RAT, inst_to_commit.dest_reg);
      }
      ++(p->stat_retired_inst);
      if (inst_to_commit.inst_num >= p->halt_inst_num) {
        p->halt = true;
      }
    }
  }

  // DUMMY CODE (for compiling, and ensuring simulation terminates!)
  //for(ii=0; ii<PIPE_WIDTH; ii++){
    //if(p->FE_latch[ii].valid){
      //if(p->FE_latch[ii].inst.inst_num >= p->halt_inst_num){
      //  p->halt=true;
      //}else{
	     // p->stat_retired_inst++;
	      //p->FE_latch[ii].valid=false;
      //}
    //}
  //}
}
  
//--------------------------------------------------------------------//




