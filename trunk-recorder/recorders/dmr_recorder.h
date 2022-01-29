#ifndef DMR_RECORDER_H
#define DMR_RECORDER_H

#define _USE_MATH_DEFINES

#include <cstdio>
#include <iostream>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>

#include <gnuradio/filter/firdes.h>
#include <gnuradio/hier_block2.h>
#include <gnuradio/io_signature.h>

#include <gnuradio/filter/fft_filter_fff.h>
#include <gnuradio/filter/pfb_arb_resampler_ccf.h>
#include <gnuradio/analog/pwr_squelch_cc.h>
#include <gnuradio/analog/pll_freqdet_cf.h>


#include <gnuradio/block.h>
#include <gnuradio/blocks/copy.h>

//#include <gnuradio/blocks/selector.h>

#if GNURADIO_VERSION < 0x030800
#include <gnuradio/analog/sig_source_c.h>
#include <gnuradio/blocks/multiply_cc.h>
#include <gnuradio/blocks/multiply_const_ff.h>
#include <gnuradio/blocks/multiply_const_ss.h>
#include <gnuradio/filter/fir_filter_ccc.h>
#include <gnuradio/filter/fir_filter_ccf.h>
#include <gnuradio/filter/fir_filter_fff.h>
#else
#include <gnuradio/analog/sig_source.h>
#include <gnuradio/blocks/multiply.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/filter/fir_filter_blk.h>
#endif


#include <boost/shared_ptr.hpp>
#include <gnuradio/block.h>
#include <gnuradio/hier_block2.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/analog/pll_freqdet_cf.h>
#include <gnuradio/msg_queue.h>
#include <gnuradio/filter/fft_filter_fff.h>

#include <gnuradio/filter/fft_filter_ccf.h>



#if GNURADIO_VERSION < 0x030800
#include <gnuradio/blocks/multiply_const_ff.h>
#include <gnuradio/filter/fir_filter_fff.h>
#else
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/filter/fir_filter_blk.h>
#endif
#include <op25_repeater/fsk4_slicer_fb.h>
#include <op25_repeater/include/op25_repeater/frame_assembler.h>


#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/message.h>
#include <gnuradio/msg_queue.h>

#include "recorder.h"
#include <gr_blocks/plugin_wrapper_impl.h>
#include <gr_blocks/nonstop_wavfile_sink.h>
#include <gr_blocks/selector.h>

class Source;
class dmr_recorder;

	#if GNURADIO_VERSION < 0x030900
  typedef boost::shared_ptr<dmr_recorder> dmr_recorder_sptr;
	#else
  typedef std::shared_ptr<dmr_recorder> dmr_recorder_sptr;
	#endif


dmr_recorder_sptr make_dmr_recorder(Source *src);
#include "../source.h"

class dmr_recorder : public gr::hier_block2, public Recorder {
  friend dmr_recorder_sptr make_dmr_recorder(Source *src);

protected:
  dmr_recorder();
  dmr_recorder(std::string type);
  virtual void initialize(Source *src);

public:
  virtual ~dmr_recorder();
  DecimSettings get_decim(long speed);
  void initialize_prefilter();
  void initialize_qpsk();
  void initialize_fsk4();
  void initialize_p25();
  void tune_offset(double f);
  void tune_freq(double f);
  bool start(Call *call);
  void stop();
  double get_freq();
  long get_talkgroup();
  std::string get_short_name();
  int get_num();
  void set_tdma(bool phase2);
  void switch_tdma(bool phase2);
  void set_tdma_slot(int slot);
  void set_record_more_transmissions(bool more);
  double since_last_write(); 
  void generate_arb_taps();
  double get_current_length();
  bool is_active();
  bool is_idle();
  bool is_squelched();
  std::vector<Transmission> get_transmission_list(); 
  State get_state();
  Rx_Status get_rx_status();
  int lastupdate();
  long elapsed();
  Source *get_source();

  void plugin_callback_handler(int16_t *samples, int sampleCount);

protected:
  State state;
  time_t timestamp;
  time_t starttime;
  long talkgroup;
  std::string short_name;
  Call *call;
  Config *config;
  Source *source;
  double chan_freq;
  double center_freq;
  bool qpsk_mod;
  bool conventional;
  double squelch_db;
  gr::analog::pwr_squelch_cc::sptr squelch;
  gr::blocks::selector::sptr modulation_selector;
  gr::blocks::copy::sptr valve;
  //gr::blocks::multiply_const_ss::sptr levels;


gr::op25_repeater::gardner_costas_cc::sptr costas_clock;
private:
  double system_channel_rate;
  double arb_rate;
  double samples_per_symbol;
  double symbol_rate;
  double initial_rate;
  long decim;
  double resampled_rate;

  int silence_frames;
  int tdma_slot;
  bool d_phase2_tdma;
  bool double_decim;
  long if1;
  long if2;
  long input_rate;
  const int phase1_samples_per_symbol = 5;
  const int phase2_samples_per_symbol = 4;
  const double phase1_symbol_rate = 4800;
  const double phase2_symbol_rate = 6000;

  std::vector<float> arb_taps;


 
  std::vector<gr_complex> bandpass_filter_coeffs;
  std::vector<float> lowpass_filter_coeffs;
  std::vector<float> cutoff_filter_coeffs;

  gr::analog::sig_source_c::sptr lo;
  gr::analog::sig_source_c::sptr bfo;
  gr::blocks::multiply_cc::sptr mixer;

  /* GR blocks */
  gr::filter::fft_filter_ccc::sptr bandpass_filter;
  gr::filter::fft_filter_ccf::sptr lowpass_filter;
  gr::filter::fft_filter_ccf::sptr cutoff_filter;

  gr::filter::pfb_arb_resampler_ccf::sptr arb_resampler;


  gr::blocks::multiply_const_ff::sptr rescale;


  /* FSK4 Stuff */

  std::vector<float> baseband_noise_filter_taps;
  std::vector<float> sym_taps;
    gr::msg_queue::sptr tune_queue;

  gr::filter::fft_filter_fff::sptr noise_filter;
  gr::filter::fir_filter_fff::sptr sym_filter;


    gr::blocks::multiply_const_ff::sptr pll_amp;
  gr::analog::pll_freqdet_cf::sptr pll_freq_lock;
    gr::op25_repeater::fsk4_demod_ff::sptr fsk4_demod;
    gr::op25_repeater::fsk4_slicer_fb::sptr slicer;


  /* P25 Decoder */
   gr::op25_repeater::frame_assembler::sptr framer;
    gr::op25_repeater::p25_frame_assembler::sptr op25_frame_assembler;
  gr::msg_queue::sptr traffic_queue;
  gr::msg_queue::sptr rx_queue;

  gr::blocks::short_to_float::sptr converter_slot0;
  gr::blocks::short_to_float::sptr converter_slot1;
  gr::blocks::multiply_const_ff::sptr levels;
  gr::blocks::nonstop_wavfile_sink::sptr wav_sink_slot0;
  gr::blocks::nonstop_wavfile_sink::sptr wav_sink_slot1;
  gr::blocks::plugin_wrapper::sptr plugin_sink;

};

#endif // ifndef dmr_recorder_H
