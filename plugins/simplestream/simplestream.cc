#include "../../trunk-recorder/plugin_manager/plugin_api.h"
#include "../../trunk-recorder/recorders/recorder.h"
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS
#include <boost/foreach.hpp>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <mutex>

using namespace boost::asio;

typedef struct plugin_t plugin_t;
typedef struct stream_t stream_t;
typedef struct audio_frame_t audio_frame_t;
typedef struct call_buffer_t call_buffer_t;

std::vector<stream_t> streams;
io_service my_tcp_io_service;
long max_tcp_index = 0;

struct audio_frame_t {
  std::vector<int16_t> samples;
  int sampleCount;
  long source_id;
  std::string call_short_name;
  long call_tgid;
  uint32_t call_freq;
  std::string call_src_tag;
  std::string call_tgid_tag;
  std::vector<long> patched_talkgroups;
  std::vector<std::string> patched_talkgroup_tags;
  int recorder_id;
  long wav_hz;
};

struct call_buffer_t {
  std::vector<audio_frame_t> frames;
  bool source_id_detected;
  long current_source_id;
  bool sending_started;
  std::string call_key;
  std::chrono::time_point<std::chrono::steady_clock> last_activity;
  std::vector<std::string> patched_talkgroup_tags;
};

std::map<std::string, call_buffer_t> call_buffers;
std::mutex call_buffers_mutex; // Thread safety for buffer operations

struct stream_t {
  long TGID;
  long tcp_index;
  std::string address;
  std::string short_name;
  long port;
  ip::udp::endpoint remote_endpoint;
  ip::tcp::socket *tcp_socket;
  bool sendTGID = false;
  bool sendJSON = false;
  bool sendCallStart = false;
  bool sendCallEnd = false;
  bool tcp = false;
  bool enable_buffering = false;
  int max_buffer_frames = 3;
};

// Global configuration
int buffer_timeout_seconds = 30; // Clean up buffers older than 30 seconds

// Helper function to create call key
std::string create_call_key(Call *call) {
  return call->get_short_name() + "_" + std::to_string(call->get_talkgroup()) + "_" + std::to_string(call->get_call_num());
}

// Helper function to create a unique per-stream key for buffering
std::string create_stream_buffer_key(Call *call, const stream_t &stream) {
  return create_call_key(call) + "_" + std::to_string(stream.TGID) + "_" + stream.address + "_" + std::to_string(stream.port) + "_" + (stream.tcp ? "tcp" : "udp");
}

// Helper function to send buffered frames
// This function is called when a source ID is detected for a buffered call.
// It sends all previously buffered audio frames with the correct source ID.
void send_buffered_frames(call_buffer_t &buffer, long source_id, ip::udp::socket &socket) {
  BOOST_LOG_TRIVIAL(debug) << "Sending " << buffer.frames.size() << " buffered frames with source_id=" << source_id;
  
  // Process each buffered frame
  for (auto &frame : buffer.frames) {    
    // Send the frame using existing logic for all matching streams
    BOOST_FOREACH (auto stream, streams) {
      if ((stream.TGID == static_cast<long>(frame.call_tgid)) || stream.TGID == 0) {
        if (0==stream.short_name.compare(frame.call_short_name) || (0==stream.short_name.compare(""))) {
          // Source ID logic: Use frame's original source_id if valid, otherwise use detected source_id
          // This handles cases where frames were buffered with different source IDs
          long stream_source_id = (frame.source_id != -1) ? frame.source_id : source_id;
          std::vector<boost::asio::const_buffer> send_buffer;
          uint32_t json_length = 0;
          if (stream.sendJSON == true) {
            json json_object = {
              {"src", stream_source_id},
              {"tgid", frame.call_tgid},
              {"freq", frame.call_freq},
              {"short_name", frame.call_short_name},
              {"src_tag", frame.call_src_tag},
              {"tgid_tag", frame.call_tgid_tag},
              {"patched_talkgroups", frame.patched_talkgroups},
              {"recorder_id", frame.recorder_id},
              {"audio_sample_rate", frame.wav_hz},
              {"event", "audio"}
            };
            
            std::string json_string = json_object.dump();
            json_length = json_string.length();
            BOOST_LOG_TRIVIAL(debug) << "Sending buffered frame JSON: " << json_string;
            send_buffer.push_back(boost::asio::buffer(&json_length, 4));
            send_buffer.push_back(boost::asio::buffer(json_string));
          }
          else if (stream.sendTGID == true) {
            send_buffer.push_back(boost::asio::buffer(&frame.call_tgid, 4));
          }
          
          send_buffer.push_back(boost::asio::buffer(frame.samples.data(), frame.sampleCount * 2));
          
          if (stream.tcp == true) {
            try {
              stream.tcp_socket->send(send_buffer);
            } catch (const boost::system::system_error& e) {
              BOOST_LOG_TRIVIAL(error) << "TCP send error for stream " << stream.TGID << ": " << e.what();
            }
          } else {
            boost::system::error_code error;
            socket.send_to(send_buffer, stream.remote_endpoint, 0, error);
            if (error) {
              BOOST_LOG_TRIVIAL(error) << "UDP send error for stream " << stream.TGID << ": " << error.message();
            }
          }
        }
      }
    }
  }
  buffer.frames.clear();
}

// Helper function to clean up stale buffers
void cleanup_stale_buffers() {
  std::lock_guard<std::mutex> lock(call_buffers_mutex);
  auto now = std::chrono::steady_clock::now();
  auto timeout_duration = std::chrono::seconds(buffer_timeout_seconds);
  
  for (auto it = call_buffers.begin(); it != call_buffers.end();) {
    auto elapsed = now - it->second.last_activity;
    if (elapsed > timeout_duration) {
      BOOST_LOG_TRIVIAL(debug) << "Cleaning up stale buffer for call: " << it->first;
      it = call_buffers.erase(it);
    } else {
      ++it;
    }
  }
}

class Simple_Stream : public Plugin_Api {
  typedef boost::asio::io_service io_service;
  io_service my_io_service;
  ip::udp::endpoint remote_endpoint;
  ip::udp::socket my_socket{my_io_service};
  public:
  
  Simple_Stream(){
      
  }

 int parse_config(json config_data) {
    for (json element : config_data["streams"]) {
      stream_t stream;
      stream.TGID = element["TGID"];
      stream.address = element["address"];
      stream.port = element["port"];
      stream.remote_endpoint = ip::udp::endpoint(ip::address::from_string(stream.address), stream.port);
      stream.sendTGID = element.value("sendTGID",false);
      stream.sendJSON = element.value("sendJSON",false);
      stream.sendCallStart = element.value("sendCallStart",false);
      stream.sendCallEnd = element.value("sendCallEnd",false);
      stream.tcp = element.value("useTCP",false);
      stream.short_name = element.value("shortName", "");
      stream.enable_buffering = element.value("enableBuffering", false);
      stream.max_buffer_frames = element.value("maxBufferFrames", 3);
      
      // Configuration validation
      if (stream.max_buffer_frames < 1) {
        BOOST_LOG_TRIVIAL(warning) << "maxBufferFrames must be >= 1, setting to 1 for stream " << stream.TGID;
        stream.max_buffer_frames = 1;
      }
      if (stream.max_buffer_frames > 10) {
        BOOST_LOG_TRIVIAL(warning) << "maxBufferFrames > 10 may cause memory issues, capping at 10 for stream " << stream.TGID;
        stream.max_buffer_frames = 10;
      }
      
      BOOST_LOG_TRIVIAL(info) << "simplestreamer will stream audio from TGID " <<stream.TGID << " on System " <<stream.short_name << " to " << stream.address <<" on port " << stream.port << " tcp is "<<stream.tcp << " buffering is " << stream.enable_buffering << " max_frames=" << stream.max_buffer_frames;
      streams.push_back(stream);
    }
    return 0;
  }
  
  int audio_stream(Call *call, Recorder *recorder, int16_t *samples, int sampleCount){
    System *call_system = call->get_system();
    int32_t call_tgid = call->get_talkgroup();
    int32_t call_src = call->get_current_source_id();
    uint32_t call_freq = call->get_freq();
    std::string call_short_name = call->get_short_name();
    std::string call_src_tag = call_system->find_unit_tag(call_src);
    
    // Debug: Log the initial source ID and transmission count
    BOOST_LOG_TRIVIAL(debug) << "audio_stream: call_src=" << call_src << " transmission_count=" << call->get_transmissions().size();
    
    std::vector<unsigned long> unsigned_patched_talkgroups = call_system->get_talkgroup_patch(call_tgid);
    std::vector<long> patched_talkgroups;
    // Convert unsigned long to signed long, preserving negative values
    for (auto tgid : unsigned_patched_talkgroups) {
      patched_talkgroups.push_back(static_cast<long>(tgid));
    }

    Recorder& local_recorder = *recorder;
    int recorder_id = local_recorder.get_num();
    long wav_hz = local_recorder.get_wav_hz();
    boost::system::error_code error;
    std::vector<std::string> patched_talkgroup_tags;
    
    // Populate talkgroup tags for all patched talkgroups (outside the stream loop)
    if (patched_talkgroups.size() == 0){
      patched_talkgroups.push_back(call_tgid);  //call_info.talkgroup may be negative - we cast stream.TGID to signed for comparison
    }
    BOOST_FOREACH (auto TGID, patched_talkgroups){
      Talkgroup* tg = call_system->find_talkgroup(static_cast<unsigned long>(TGID));
      if (tg != nullptr && !tg->alpha_tag.empty()) {
        patched_talkgroup_tags.push_back(tg->alpha_tag);
      }
    }
    
    BOOST_FOREACH (auto stream, streams){
      if (0==stream.short_name.compare(call_short_name) || (0==stream.short_name.compare(""))){ //Check if shortName matches or is not specified
        BOOST_FOREACH (auto TGID, patched_talkgroups){
          if ((TGID==static_cast<long>(stream.TGID)) || stream.TGID==0){  //setting TGID to 0 in the config file will stream everything
            
            // BUFFERING LOGIC: Handle per-stream buffering for race condition mitigation
            // This addresses the issue where audio_stream() is called before source ID propagation
            if (stream.enable_buffering) {
              std::string call_key = create_stream_buffer_key(call, stream);
              
              // Thread-safe buffer access
              std::lock_guard<std::mutex> lock(call_buffers_mutex);
              
              // Cache buffer lookup to avoid multiple map lookups
              auto buffer_it = call_buffers.find(call_key);
              if (buffer_it == call_buffers.end()) {
                // Initialize buffer if it doesn't exist
                call_buffer_t new_buffer;
                new_buffer.source_id_detected = false;
                new_buffer.current_source_id = -1;
                new_buffer.sending_started = false;
                new_buffer.call_key = call_key;
                new_buffer.last_activity = std::chrono::steady_clock::now();
                buffer_it = call_buffers.insert({call_key, new_buffer}).first;
              }
              
              call_buffer_t &buffer = buffer_it->second;
              buffer.last_activity = std::chrono::steady_clock::now(); // Update activity timestamp
              
              // SOURCE ID DETECTION: Check if source ID was just detected for this buffer
              if (call_src != -1 && !buffer.source_id_detected) {
                buffer.source_id_detected = true;
                buffer.current_source_id = call_src;
                BOOST_LOG_TRIVIAL(debug) << "Source ID detected for " << call_key << ": " << call_src;
                
                // FLUSH BUFFER: Send all buffered frames with the detected source ID
                send_buffered_frames(buffer, call_src, my_socket);
                buffer.sending_started = true;
              }
              
              // BUFFERING PHASE: If we're still waiting for source ID detection
              if (!buffer.sending_started) {
                // BUFFER FRAME: Store audio frame for later transmission
                audio_frame_t frame;
                frame.samples.assign(samples, samples + sampleCount);
                frame.sampleCount = sampleCount;
                frame.source_id = call_src;  // May be -1 if source not yet detected
                frame.call_short_name = call_short_name;
                frame.call_tgid = call_tgid;
                frame.call_freq = call_freq;
                frame.call_src_tag = call_src_tag;
                frame.call_tgid_tag = call->get_talkgroup_tag();
                frame.patched_talkgroups = patched_talkgroups;
                frame.recorder_id = recorder->get_num();
                frame.wav_hz = recorder->get_wav_hz();
                if (patched_talkgroup_tags.size() > 0){
                  frame.patched_talkgroup_tags = patched_talkgroup_tags;
                }
                
                buffer.frames.push_back(frame);
                
                // BUFFER LIMIT CHECK: Prevent infinite buffering if source ID never detected
                if (buffer.frames.size() >= static_cast<size_t>(stream.max_buffer_frames)) {
                  BOOST_LOG_TRIVIAL(debug) << "Max buffer size reached for " << call_key << ", sending with current source ID";
                  send_buffered_frames(buffer, call_src, my_socket);
                  buffer.sending_started = true;
                }
                
                continue; // Skip immediate sending for this stream (frame is buffered)
              }
            }
            
            // Send immediately (for non-buffering streams or after buffering is complete)
            BOOST_LOG_TRIVIAL(debug) << "got " <<sampleCount <<" samples - " <<sampleCount*2<<" bytes from recorder "<<recorder_id<<" for TGID "<<TGID;
            json json_object;
            std::string json_string;
            std::vector<boost::asio::const_buffer> send_buffer;
            uint32_t json_length = 0;
            if (stream.sendJSON==true){
              //create JSON metadata
              json_object = {
                 {"src", call_src},
                 {"src_tag",call_src_tag},
                 {"talkgroup", TGID},
                 {"patched_talkgroups",patched_talkgroups},
                 {"freq", call_freq},
                 {"short_name", call_short_name},
                 {"audio_sample_rate",wav_hz},
                 {"event","audio"},
              };
              if (patched_talkgroup_tags.size() > 0){
                json_object["patched_talkgroup_tags"] = patched_talkgroup_tags;
              }
              json_string = json_object.dump();
              json_length = json_string.length();  //determine length in bytes
              //BOOST_LOG_TRIVIAL(debug) << "json_length is " <<json_length <<" bytes";
              send_buffer.push_back(buffer(&json_length,4));  //prepend length of the json data
              send_buffer.push_back(buffer(json_string));  //prepend json data
              //BOOST_LOG_TRIVIAL(debug) << "json_string is " <<json_string;
            }
            else if (stream.sendTGID==true){
              send_buffer.push_back(buffer(&TGID,4));  //prepend 4 byte long tgid to the audio data
            }
            send_buffer.push_back(buffer(samples, sampleCount*2));
            if(stream.tcp == true){
              try {
                try {
                  stream.tcp_socket->send(send_buffer);
                } catch (const boost::system::system_error& e) {
                  BOOST_LOG_TRIVIAL(error) << "TCP send error for stream " << stream.TGID << ": " << e.what();
                }
              } catch (const boost::system::system_error& e) {
                BOOST_LOG_TRIVIAL(error) << "TCP send error for stream " << stream.TGID << ": " << e.what();
              }
            }
            else{
              my_socket.send_to(send_buffer, stream.remote_endpoint, 0, error);
              if (error) {
                BOOST_LOG_TRIVIAL(error) << "UDP send error for stream " << stream.TGID << ": " << error.message();
              }
            }
          }
        }
      }
    }
    return 0;
  }

  int call_start(Call *call){
    boost::system::error_code error;
    System *call_system = call->get_system();
    int32_t call_tgid = call->get_talkgroup();
    int32_t call_src = call->get_current_source_id();
    uint32_t call_freq = call->get_freq();
    std::string call_short_name = call->get_short_name();
    std::string call_src_tag = call_system->find_unit_tag(call_src);
    std::string call_tgid_tag = call->get_talkgroup_tag();
    std::vector<unsigned long> unsigned_patched_talkgroups = call_system->get_talkgroup_patch(call_tgid);
    std::vector<long> patched_talkgroups;
    // Convert unsigned long to signed long, preserving negative values
    for (auto tgid : unsigned_patched_talkgroups) {
      patched_talkgroups.push_back(static_cast<long>(tgid));
    }

    if(call_src == -1){
      if(call->get_transmissions().size() > 0){
        // Get the source from the most recent transmission
        auto transmissions = call->get_transmissions();
        call_src = transmissions.back().source;
      }
    }
    
    // Populate talkgroup tags for all patched talkgroups (outside the stream loop)
    std::vector<std::string> patched_talkgroup_tags;
    if (patched_talkgroups.size() == 0){
      patched_talkgroups.push_back(call_tgid);  //call_info.talkgroup may be negative - we cast stream.TGID to signed for comparison
    }
    BOOST_FOREACH (auto TGID, patched_talkgroups){
      Talkgroup* this_tg = call_system->find_talkgroup(static_cast<unsigned long>(TGID));
      if (this_tg != nullptr && !this_tg->alpha_tag.empty()) {
        patched_talkgroup_tags.push_back(this_tg->alpha_tag);
      }
    }
    
    BOOST_FOREACH (auto stream, streams){
      if (stream.sendJSON == true && stream.sendCallStart == true){
        if (0==stream.short_name.compare(call_short_name) || (0==stream.short_name.compare(""))){ //Check if shortName matches or is not specified
          BOOST_FOREACH (auto TGID, patched_talkgroups){
            if ((TGID==static_cast<long>(stream.TGID)) || stream.TGID==0){  //setting TGID to 0 in the config file will stream everything
              json json_object;
              std::string json_string;
              std::vector<boost::asio::const_buffer> send_buffer;
              uint32_t json_length = 0;
              if (stream.sendJSON==true){
                //create JSON metadata
                json_object = {
                   {"src", call_src},
                   {"src_tag", call_src_tag},
                   {"talkgroup", call_tgid},
                   {"talkgroup_tag",call_tgid_tag},
                   {"patched_talkgroups",patched_talkgroups},
                   {"patched_talkgroup_tags",patched_talkgroup_tags},
                   {"freq", call_freq},
                   {"short_name", call_short_name},
                   {"event","call_start"},
                };
                json_string = json_object.dump();
                json_length = json_string.length();  //determine length in bytes
                send_buffer.push_back(buffer(&json_length,4));  //prepend length of the json data
                send_buffer.push_back(buffer(json_string));  //prepend json data
              }
              if(stream.tcp == true){
                try {
                  stream.tcp_socket->send(send_buffer);
                } catch (const boost::system::system_error& e) {
                  BOOST_LOG_TRIVIAL(error) << "TCP send error for stream " << stream.TGID << ": " << e.what();
                }
              }
              else{
                my_socket.send_to(send_buffer, stream.remote_endpoint, 0, error);
              }
            }
          }
        }
      }
    }
    return 0;
  }

  int call_end(Call_Data_t call_info) {
    // Clean up any buffered frames for this call (including stream-specific buffers)
    std::string base_call_key = call_info.short_name + "_" + std::to_string(call_info.talkgroup) + "_" + std::to_string(call_info.call_num);
    
    // Thread-safe buffer cleanup
    std::lock_guard<std::mutex> lock(call_buffers_mutex);
    
    // Remove all buffers that start with the base call key (handles stream-specific keys)
    for (auto it = call_buffers.begin(); it != call_buffers.end();) {
      if (it->first.find(base_call_key) == 0) {
        BOOST_LOG_TRIVIAL(debug) << "Clearing buffer for call: " << it->first;
        it = call_buffers.erase(it);
      } else {
        ++it;
      }
    }
    
    boost::system::error_code error;
    BOOST_FOREACH (auto stream, streams){
      if (stream.sendJSON == true && stream.sendCallEnd == true){
        if (0==stream.short_name.compare(call_info.short_name) || (0==stream.short_name.compare(""))){ //Check if shortName matches or is not specified
          std::vector<long> patched_talkgroups;
          if (call_info.patched_talkgroups.size() == 0){
            patched_talkgroups.push_back(call_info.talkgroup); //call_info.talkgroup may be negative - we cast stream.TGID to signed for comparison
          } else {
            // Convert existing unsigned long values to signed long
            for (auto tgid : call_info.patched_talkgroups) {
              patched_talkgroups.push_back(static_cast<long>(tgid));
            }
          }
          BOOST_FOREACH (auto TGID, patched_talkgroups){
            if ((TGID == static_cast<long>(stream.TGID)) || stream.TGID==0){  //setting TGID to 0 in the config file will stream everything
              json json_object;
              std::string json_string;
              std::vector<boost::asio::const_buffer> send_buffer;
              uint32_t json_length = 0;
              if (stream.sendJSON==true){
                //create JSON metadata
                json_object = {
                   {"talkgroup", call_info.talkgroup},
                   {"patched_talkgroups",patched_talkgroups},
                   {"freq", call_info.freq},
                   {"short_name", call_info.short_name},
                   {"event","call_end"},
                };
                json_string = json_object.dump();
                json_length = json_string.length();  //determine length in bytes
                send_buffer.push_back(buffer(&json_length,4));  //prepend length of the json data
                send_buffer.push_back(buffer(json_string));  //prepend json data
              }
              if(stream.tcp == true){
                try {
                  stream.tcp_socket->send(send_buffer);
                } catch (const boost::system::system_error& e) {
                  BOOST_LOG_TRIVIAL(error) << "TCP send error for stream " << stream.TGID << ": " << e.what();
                }
              }
              else{
                my_socket.send_to(send_buffer, stream.remote_endpoint, 0, error);
              }
            }
          }
        }
      }
    }
    return 0;
  }

  int start(){
    BOOST_FOREACH (auto& stream, streams){
      if (stream.tcp == true){
        ip::tcp::socket *my_tcp_socket = new ip::tcp::socket{my_tcp_io_service};
        stream.tcp_socket = my_tcp_socket;
        stream.tcp_socket->connect(ip::tcp::endpoint( boost::asio::ip::address::from_string(stream.address), stream.port ));
      }
    }
    my_socket.open(ip::udp::v4());
    return 0;
  }
  
  int stop(){
    BOOST_FOREACH (auto& stream, streams){
      if (stream.tcp == true){
        stream.tcp_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        stream.tcp_socket->close();
      }
    }
    my_socket.close();
    return 0;
  }

  static boost::shared_ptr<Simple_Stream> create() {
    return boost::shared_ptr<Simple_Stream>(
        new Simple_Stream());
  }
  
  int poll_one(plugin_t * const plugin) {
    // Clean up stale buffers to prevent memory leaks
    cleanup_stale_buffers();
    return 0;
  }
};

BOOST_DLL_ALIAS(
    Simple_Stream::create, // <-- this function is exported with...
    create_plugin             // <-- ...this alias name
)
