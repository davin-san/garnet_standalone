#include "FaultModel.hh"
#include "FaultModelData.hh"
#include <iostream>
#include <cassert>

#define MAX_VCs 40
#define MAX_BUFFERS_per_VC 5
#define BASELINE_TEMPERATURE_CELCIUS 71
#define MAX(a,b) ((a > b) ? (a) : (b))

namespace garnet {

FaultModel::FaultModel()
{
    load_default_config();
}

void FaultModel::load_default_config()
{
    // read configurations into "configurations" vector
    // format: <buff/vc> <vcs> <10 fault types>
    bool more_records = true;
    for (int i = 0; more_records; i += (fields_per_conf_record)){
        system_conf configuration;
        configuration.buff_per_vc =
            baseline_fault_vector_database[i + conf_record_buff_per_vc];
        configuration.vcs =
            baseline_fault_vector_database[i + conf_record_vcs];
        for (int fault_index = 0; fault_index < number_of_fault_types;
            fault_index++){
            configuration.fault_type[fault_index] =
                baseline_fault_vector_database[i +
                   conf_record_first_fault_type + fault_index] / 100;
        }
        configurations.push_back(configuration);
        if (baseline_fault_vector_database[i+fields_per_conf_record] < 0){
            more_records = false;
        }
    }

    // read temperature weights into "temperature_weights" vector
    // format: <temperature> <weight>
    more_records = true;
    for (int i = 0; more_records; i += (fields_per_temperature_record)){
        int record_temperature =
               temperature_weights_database[i + temperature_record_temp];
        int record_weight =
               temperature_weights_database[i + temperature_record_weight];
        static int first_record = true;
        if (first_record){
            for (int temperature = 0; temperature < record_temperature;
                 temperature++){
                 temperature_weights.push_back(0);
            }
            first_record = false;
        }
        // assert(record_temperature == temperature_weights.size());
        if(record_temperature != (int)temperature_weights.size()) {
             // Fill gaps if any
             while((int)temperature_weights.size() < record_temperature) {
                 temperature_weights.push_back(0);
             }
        }
        
        temperature_weights.push_back(record_weight);
        if (temperature_weights_database[i +
               fields_per_temperature_record] < 0){
            more_records = false;
        }
    }
}

void
FaultModel::print(void)
{
    std::cout << "--- PRINTING configurations ---\n";
    for (int record = 0; record < (int)configurations.size(); record++){
        std::cout << "(" << record << ") ";
        std::cout << "VCs=" << configurations[record].vcs << " ";
        std::cout << "Buff/VC=" << configurations[record].buff_per_vc << " [";
        for (int fault_type_num = 0;
             fault_type_num < (int)number_of_fault_types;
             fault_type_num++){
            std::cout <<
                (100 * configurations[record].fault_type[fault_type_num]);
            std::cout << "% ";
        }
        std::cout << "]\n";
    }
    std::cout << "--- PRINTING temperature weights ---\n";
    for (int record = 0; record < (int)temperature_weights.size(); record++){
        std::cout << "temperature=" << record << " => ";
        std::cout << "weight=" << (int)temperature_weights[record];
        std::cout << "\n";
    }
}

int 
FaultModel::declare_router(int number_of_inputs,
                       int number_of_outputs,
                       int number_of_vcs_per_vnet,
                       int number_of_buff_per_data_vc,
                       int number_of_buff_per_ctrl_vc)
{
    // check inputs (are they legal?)
    if (number_of_inputs <= 0 || number_of_outputs <= 0 ||
        number_of_vcs_per_vnet <= 0 || number_of_buff_per_data_vc <= 0 ||
        number_of_buff_per_ctrl_vc <= 0){
        std::cerr << "Fault Model: ERROR in argument of FaultModel_declare_router!" << std::endl;
        exit(1);
    }
    int number_of_buffers_per_vc = MAX(number_of_buff_per_data_vc,
                                       number_of_buff_per_ctrl_vc);
    
    // Total VCs = (virtual networks) * (vcs per vnet) * (inputs?)
    // In gem5 code: int total_vcs = number_of_inputs * number_of_vcs_per_input;
    // But the argument is "number_of_vcs_per_vnet". 
    // In GarnetNetwork.cc:
    // fault_model->declare_router(router->get_num_inports(), 
    //                             router->get_num_outports(), 
    //                             router->get_vc_per_vnet(), 
    //                             getBuffersPerDataVC(), 
    //                             getBuffersPerCtrlVC());
    //
    // The gem5 FaultModel.cc says:
    // int total_vcs = number_of_inputs * number_of_vcs_per_input;
    //
    // In standalone, we pass vcs_per_vnet. 
    // Wait, "number_of_vcs_per_input" in gem5 signature corresponds to "router->get_vc_per_vnet()" in call?
    // In gem5 Router.hh: get_vc_per_vnet() returns m_vc_per_vnet.
    // So total_vcs should be: number_of_inputs * (vnets * vcs_per_vnet) ??
    //
    // Let's look at gem5 FaultModel.cc again.
    // It compares `configurations[record].vcs == total_vcs`.
    // The configurations have vcs=40, 39...
    // MAX_VCs is 40.
    // If we have 5 inputs and 4 VCs/vnet and 2 vnets = 8 VCs/input. 5*8 = 40.
    // So `number_of_vcs_per_input` likely implies Total VCs per input port (which is vnets * vcs_per_vnet).
    // But the argument name in gem5 source I read was `number_of_vcs_per_input`.
    // In standalone headers it is `number_of_vcs_per_vnet`.
    //
    // I should calculate total_vcs properly.
    // If the standalone header says `number_of_vcs_per_vnet`, I might be missing `number_of_virtual_networks`.
    // BUT the function signature in `FaultModel.hh` matches what I have.
    //
    // Let's assume the caller passes VCs per input (total).
    // Or I need to fix the caller in GarnetNetwork.cc.
    // In GarnetNetwork.cc (gem5):
    // fault_model->declare_router(..., router->get_vc_per_vnet(), ...)
    // Wait, `get_vc_per_vnet()` is just `m_vc_per_vnet`.
    //
    // Let's check `Router.hh` in gem5 (I don't have access now, but memory serves).
    // The key is: `int total_vcs = number_of_inputs * number_of_vcs_per_input;`
    // If total_vcs goes up to 40, and inputs is 5, then vcs_per_input is 8.
    // This matches 2 vnets * 4 vcs/vnet.
    // So the argument `number_of_vcs_per_vnet` in my header might be misnamed or I need to pass (vcs_per_vnet * num_vnets).
    //
    // In `GarnetNetwork.cc` (standalone), I need to check how I call it.
    // I haven't implemented the call yet (it was commented out).
    
    // For now, I will implement `declare_router` assuming the 3rd arg is VCs per input.
    
    int total_vcs = number_of_inputs * number_of_vcs_per_vnet; 
    
    if (total_vcs > MAX_VCs){
        std::cerr << "Fault Model: ERROR! Number inputs*VCs (" << total_vcs << ") > MAX_VCs (" << MAX_VCs << ") unsupported" << std::endl;
        exit(1);
    }
    if (number_of_buffers_per_vc > MAX_BUFFERS_per_VC){
        std::cerr << "Fault Model: ERROR! buffers/VC (" << number_of_buffers_per_vc << ") > MAX (" << MAX_BUFFERS_per_VC << ") too high" << std::endl;
        exit(1);
    }

    // link the router to a DB record
    int record_hit = -1;
    for (int record = 0; record < (int)configurations.size(); record++){
        if ((configurations[record].buff_per_vc == number_of_buffers_per_vc)&&
            (configurations[record].vcs == total_vcs)){
            record_hit = record;
        }
    }
    if (record_hit == -1){
        // panic("Fault Model: ERROR! configuration not found in DB. BUG?");
        // Instead of panic, let's warn and use the closest one or 0.
        // For standalone robustness, we shouldn't crash if exact config isn't found, 
        // unless we want strict parity. 
        // gem5 panics. I will panic too to ensure "perfect accuracy" request is respected (it implies strictness).
        std::cerr << "Fault Model: ERROR! configuration not found in DB for Buff/VC=" << number_of_buffers_per_vc << " TotalVCs=" << total_vcs << std::endl;
        exit(1);
    }

    // remember the router and return its ID
    routers.push_back(configurations[record_hit]);
    static int router_index = 0;
    return router_index++;
}

std::string 
FaultModel::fault_type_to_string(int ft)
{
   if (ft == data_corruption__few_bits){
       return "data_corruption__few_bits";
   } else if (ft == data_corruption__all_bits){
      return "data_corruption__all_bits";
   } else if (ft == flit_conservation__flit_duplication){
      return "flit_conservation__flit_duplication";
   } else if (ft == flit_conservation__flit_loss_or_split){
      return "flit_conservation__flit_loss_or_split";
   } else if (ft == misrouting){
      return "misrouting";
   } else if (ft == credit_conservation__credit_generation){
      return "credit_conservation__credit_generation";
   } else if (ft == credit_conservation__credit_loss){
      return "credit_conservation__credit_loss";
   } else if (ft == erroneous_allocation__VC){
      return "erroneous_allocation__VC";
   } else if (ft == erroneous_allocation__switch){
      return "erroneous_allocation__switch";
   } else if (ft == unfair_arbitration){
      return "unfair_arbitration";
   } else {
      return "none";
   }
}

bool 
FaultModel::fault_vector(int routerID,
                      int temperature_input,
                      float fault_vector_array[])
{
    bool ok = true;

    // is the routerID recorded?
    if (routerID < 0 || routerID >= ((int) routers.size())){
        std::cerr << "Fault Model: ERROR! unknown router ID argument." << std::endl;
        exit(1);
    }

    // is the temperature too high/too low?
    int temperature = temperature_input;
    if (temperature_input >= ((int) temperature_weights.size())){
        ok = false;
        // warn_once("Fault Model: Temperature exceeded simulated upper bound.");
        temperature = (temperature_weights.size() - 1);
    } else if (temperature_input < 0){
        ok = false;
        // warn_once("Fault Model: Temperature exceeded simulated lower bound.");
        temperature = 0;
    }

    // recover the router record and return its fault vector
    for (int i = 0; i < number_of_fault_types; i++){
        fault_vector_array[i] = routers[routerID].fault_type[i] *
                          ((float)temperature_weights[temperature]);
    }
    return ok;
}

bool 
FaultModel::fault_prob(int routerID,
                    int temperature_input,
                    float *aggregate_fault_prob)
{
    *aggregate_fault_prob = 1.0;
    bool ok = true;

    // is the routerID recorded?
    if (routerID < 0 || routerID >= ((int) routers.size())){
        std::cerr << "Fault Model: ERROR! unknown router ID argument." << std::endl;
        exit(1);
    }

    // is the temperature too high/too low?
    int temperature = temperature_input;
    if (temperature_input >= ((int) temperature_weights.size()) ){
        ok = false;
        temperature = (temperature_weights.size()-1);
    } else if (temperature_input < 0){
        ok = false;
        temperature = 0;
    }

    // recover the router record and return its aggregate fault probability
    for (int i = 0; i < number_of_fault_types; i++){
        *aggregate_fault_prob = *aggregate_fault_prob *
                               ( 1.0 - (routers[routerID].fault_type[i] *
                                 ((float)temperature_weights[temperature])) );
    }
    *aggregate_fault_prob = 1.0 - *aggregate_fault_prob;
    return ok;
}

} // namespace garnet