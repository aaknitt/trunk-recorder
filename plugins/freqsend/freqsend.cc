#include "../../trunk-recorder/plugin_manager/plugin_api.h"
#include "../../trunk-recorder/recorders/recorder.h"
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS
#include <boost/foreach.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/mutex.hpp>
#include <map>
#include <unordered_set>
#include <time.h>
#include <curl/curl.h>


/*TO DO*/
// If no recorders are active, clear out freqlist
// change config file and parsing to only use one IP, not one for each system
// add patch following for TGIDs
// send frequency list on event in addition to polling
// only send in polling loop if last send more than X seconds ago
// add timeout in case call_end was missed

using namespace boost::asio;

typedef struct plugin_t plugin_t;
typedef struct system_t system_t;
typedef struct call_info_t call_info_t;


std::vector<system_t> systems;
std::vector<double> freqlist;
time_t lastSendTime = time(NULL);
boost::mutex freqlist_mutex;

struct plugin_t {
  Config* config;
};

struct call_info_t {
  std::vector<unsigned long> patched_talkgroups;
  std::string short_name;
  std::string system_type;
};

struct system_t {
  std::string short_name;
  std::unordered_set<unsigned long> TGIDs;
  std::unordered_set<unsigned long> units;
  long port;
  std::string address;
  std::map<unsigned long,double> current_TGID_freqs;
};


struct data {
  char trace_ascii; // 1 or 0 
};


class Freq_Send : public Plugin_Api {

  public:
  
  Freq_Send(){
      
  }
    
  int call_start(Call *call) {
    boost::mutex::scoped_lock lock(freqlist_mutex);
    unsigned long talkgroup_num = call->get_talkgroup();
    unsigned long unit_num = call->get_current_source_id();
    double freq = call->get_freq();
    std::string short_name = call->get_short_name();
    //BOOST_LOG_TRIVIAL(info) << "call_start called in freqsend plugin for TGID "<< talkgroup_num << " on system " << short_name << " from unit "<<unit_num<<" on freq "<< freq;
    BOOST_FOREACH (system_t &system, systems){
      if (0==system.short_name.compare(short_name)){
        //This transmission is on a system we care about
        //BOOST_LOG_TRIVIAL(info) << "system.TGIDs.count("<<talkgroup_num<<") is "<<system.TGIDs.count(talkgroup_num);
        std::map<unsigned long,double> current_TGID_freqs = system.current_TGID_freqs;
        if(system.TGIDs.count(talkgroup_num)> 0 || system.units.count(unit_num) > 0){
          current_TGID_freqs[talkgroup_num] = freq;
          if (system.units.count(unit_num) > 0){
            BOOST_LOG_TRIVIAL(info) << "adding "<<freq<<" to current_TGID_freqs["<<talkgroup_num<<"] because unit "<<unit_num<<" was found";
          }
        }
        else{
          current_TGID_freqs.erase(talkgroup_num);
        }
        system.current_TGID_freqs = current_TGID_freqs;
      }
    }
    return 0;
  }
  
  /*
  int audio_stream(Recorder *recorder, int16_t *samples, int sampleCount){
    //BOOST_LOG_TRIVIAL(info)<<"recorder "<<recorder->get_num()<< " is on freq "<<recorder->get_freq()<< " for TGID "<<recorder->get_talkgroup();
	BOOST_FOREACH (system_t &system, systems){
	  if (0==system.short_name.compare(recorder->get_short_name())){
		  system.current_TGID_freqs[recorder->get_talkgroup()] = recorder->get_freq();
	  }
	}
	return 0;
  }
  */
    
  int call_end(Call_Data_t call_info) {
    boost::mutex::scoped_lock lock(freqlist_mutex);
    unsigned long talkgroup_num = call_info.talkgroup;
    std::string short_name = call_info.short_name;
    //BOOST_LOG_TRIVIAL(info) << "call_end called in freqsend plugin on TGID " << talkgroup_num << " on system " <<short_name ;
    BOOST_FOREACH (system_t &system, systems){
      std::map<unsigned long,double> current_TGID_freqs = system.current_TGID_freqs;
      if (0==system.short_name.compare(short_name)){
        //This transmission was on a system we care about
		size_t num_erased;
        num_erased = current_TGID_freqs.erase(talkgroup_num);
		//BOOST_LOG_TRIVIAL(info) << "ERASING " << talkgroup_num << " current_TGID_freqs in call_end with result "<<num_erased;
      }
      system.current_TGID_freqs = current_TGID_freqs;
    }
    return 0;
  }

  int parse_config(boost::property_tree::ptree &cfg) {
    BOOST_FOREACH (boost::property_tree::ptree::value_type &node, cfg.get_child("systems")) {
      system_t system;
      system.short_name = node.second.get<std::string>("shortName", "");
      BOOST_FOREACH (boost::property_tree::ptree::value_type &subnode, node.second.get_child("TGIDs")){
        system.TGIDs.insert(subnode.second.get<unsigned long>("",0));
        BOOST_LOG_TRIVIAL(info) << "adding TGID "<<subnode.second.get<unsigned long>("",0)<<" to "<<system.short_name;
      }
      BOOST_FOREACH (boost::property_tree::ptree::value_type &subnode, node.second.get_child("units")){
        system.units.insert(subnode.second.get<unsigned long>("",0));
        BOOST_LOG_TRIVIAL(info) << "adding unit ID "<<subnode.second.get<unsigned long>("",0)<<" to "<<system.short_name;
      }
      system.address = node.second.get<std::string>("address");
      system.port = node.second.get<long>("port");
      systems.push_back(system);
    }
    return 0;
  }

  int poll_one(){
    time_t current_time = time(NULL);
    float timeDiff = current_time - lastSendTime;
    if (timeDiff >= .2){
      lastSendTime = current_time;
      upload3();
    }
    return 0;
  }

  int start(){
  upload3();
  return 0;
  }
  
  int stop(){
    return 0;
  }
  


  int upload3(void){
    //Since I'm too fucking dense to figure out how to do this using the built in curl libraries, I'll call curl externally instead.  This is really stupid.  
    char shell_command[500];
    //curl -H "Content-Type: text/xml" -d @sample.xml -X POST 192.168.1.219:8080 -v
    std::string sendstring = "<?xml version='1.0'?><methodCall><methodName>set_freq_list</methodName><params><param><value><array><data>";
    BOOST_FOREACH (system_t system, systems){
      BOOST_FOREACH (auto& element, system.current_TGID_freqs){
      sendstring+="<value><double>";
      sendstring+=boost::lexical_cast<std::string>(element.second);
      sendstring+="</double></value>";
      //BOOST_LOG_TRIVIAL(info) <<"adding "<<element.second<<" to sendstring";
      }
    }
    sendstring+="</data></array></value></param></params></methodCall>";
    sprintf(shell_command, "curl --silent --output /dev/null --show-error --fail -H \"Content-Type: text/xml\" -d \"%s\" -X POST 192.168.1.219:8080 " , sendstring.c_str());
    int rc =  system(shell_command);
    return 0;
  }


  static boost::shared_ptr<Freq_Send> create() {
    return boost::shared_ptr<Freq_Send>(
        new Freq_Send());
  }
};

BOOST_DLL_ALIAS(
    Freq_Send::create, // <-- this function is exported with...
    create_plugin             // <-- ...this alias name
)
