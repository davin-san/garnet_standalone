
#ifndef __FAULT_MODEL_HH__
#define __FAULT_MODEL_HH__

#include <vector>
#include <string>

namespace garnet {

class FaultModel
{
  public:
    FaultModel();
    ~FaultModel() = default;

    /************************************************************************/
    /**********  THE FAULT TYPES SUPPORTED BY THE FAULT MODEL ***************/
    /************************************************************************/

    enum fault_type
    {
        data_corruption__few_bits,
        data_corruption__all_bits,
        flit_conservation__flit_duplication,
        flit_conservation__flit_loss_or_split,
        misrouting,
        credit_conservation__credit_generation,
        credit_conservation__credit_loss,
        erroneous_allocation__VC,
        erroneous_allocation__switch,
        unfair_arbitration,
        number_of_fault_types
    };

    /************************************************************************/
    /********************  INTERFACE OF THE FAULT MODEL *********************/
    /************************************************************************/

    enum conf_record_format
    {
        conf_record_buff_per_vc,
        conf_record_vcs,
        conf_record_first_fault_type,
        conf_record_last_fault_type = conf_record_first_fault_type + number_of_fault_types - 1,
        fields_per_conf_record
    };

    enum temperature_record_format
    {
        temperature_record_temp,
        temperature_record_weight,
        fields_per_temperature_record
    };

    struct system_conf
    {
        int vcs;
        int buff_per_vc;
        float fault_type[number_of_fault_types];
    };

    int declare_router(int number_of_inputs,
                       int number_of_outputs,
                       int number_of_vcs_per_vnet,
                       int number_of_buff_per_data_vc,
                       int number_of_buff_per_ctrl_vc);

    std::string fault_type_to_string(int fault_type_index);

    bool fault_vector(int routerID,
                      int temperature,
                      float fault_vector[]);

    bool fault_prob(int routerID,
                    int temperature,
                    float *aggregate_fault_prob);

    void print(void);

  private:
    std::vector <system_conf> configurations;
    std::vector <system_conf> routers;
    std::vector <int> temperature_weights;
    
    void load_default_config();
};

} // namespace garnet

#endif // __FAULT_MODEL_HH__
