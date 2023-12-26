/* -*- c++ -*- */
/*
 * Copyright 2019 Free Software Foundation Inc..
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "signal_detector_cvf_impl.h"
#include <gnuradio/io_signature.h>
#include <volk/volk.h>
#include <cmath>

/*
namespace gr {
namespace inspector {
*/
signal_detector_cvf::sptr signal_detector_cvf::make(double samp_rate,
                                                    int fft_len,
                                                    int window_type,
                                                    float threshold,
                                                    float sensitivity,
                                                    bool auto_threshold,
                                                    float average,
                                                    float quantization,
                                                    float min_bw,
                                                    const char* filename)
{
    return gnuradio::get_initial_sptr(new signal_detector_cvf_impl(samp_rate,
                                                                   fft_len,
                                                                   window_type,
                                                                   threshold,
                                                                   sensitivity,
                                                                   auto_threshold,
                                                                   average,
                                                                   quantization,
                                                                   min_bw,
                                                                   filename));
}


/*
 * The private constructor
 */
signal_detector_cvf_impl::signal_detector_cvf_impl(double samp_rate,
                                                   int fft_len,
                                                   int window_type,
                                                   float threshold,
                                                   float sensitivity,
                                                   bool auto_threshold,
                                                   float average,
                                                   float quantization,
                                                   float min_bw,
                                                   const char* filename)
    : sync_decimator("signal_detector_cvf",
                     gr::io_signature::make(1, 1, sizeof(gr_complex)),
                     gr::io_signature::make(0, 0, sizeof(float) * fft_len),
                     fft_len)
{

    // set properties
    d_samp_rate = samp_rate;
    d_fft_len = fft_len;
    d_window_type = (gr::fft::window::win_type)window_type;
    d_threshold = threshold;
    d_sensitivity = sensitivity;
    d_auto_threshold = auto_threshold;
    d_average = average;
    d_quantization = quantization;
    d_min_bw = min_bw;
    d_filename = filename;
    d_detected_signals = std::vector<Detected_Signal>();

    // allocate buffers
    d_tmpbuf =
        static_cast<float*>(volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment()));
    d_tmp_pxx =
        static_cast<float*>(volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment()));
    d_pxx =
        static_cast<float*>(volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment()));
    d_pxx_out = (float*)volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment());
    d_fft = new gr::fft::fft_complex_fwd(fft_len, true);

    d_avg_filter.resize(d_fft_len);
    build_window();
    for (unsigned int i = 0; i < d_fft_len; i++) {
        d_avg_filter[i].set_taps(d_average);
    }

    d_freq = build_freq();
}

/*
 * Our virtual destructor.
 */
signal_detector_cvf_impl::~signal_detector_cvf_impl()
{
    //logfile.close();
    delete d_fft;
    volk_free(d_tmpbuf);
    volk_free(d_tmp_pxx);
    volk_free(d_pxx);
    volk_free(d_pxx_out);
}

void signal_detector_cvf_impl::set_fft_len(int fft_len)
{
    signal_detector_cvf_impl::d_fft_len = fft_len;
    delete d_fft;
    volk_free(d_tmpbuf);
    volk_free(d_tmp_pxx);
    volk_free(d_pxx);
    volk_free(d_pxx_out);
    d_fft = new gr::fft::fft_complex_fwd(fft_len, true);
    d_tmpbuf =
        static_cast<float*>(volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment()));
    d_tmp_pxx =
        static_cast<float*>(volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment()));
    d_pxx =
        static_cast<float*>(volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment()));
    d_pxx_out = (float*)volk_malloc(sizeof(float) * d_fft_len, volk_get_alignment());
    d_avg_filter.resize(d_fft_len);
    build_window();
    for (unsigned int i = 0; i < d_fft_len; i++) {
        d_avg_filter[i].set_taps(d_average);
    }
    set_decimation(fft_len);

    d_freq = build_freq();
}

void signal_detector_cvf_impl::set_window_type(int window)
{
    signal_detector_cvf_impl::d_window_type =
        static_cast<gr::fft::window::win_type>(window);
    build_window();
}




// calculate periodogram and save in specified array
void signal_detector_cvf_impl::periodogram(float* pxx, const gr_complex* signal)
{
    if (d_window.size()) {
        // window signal
        volk_32fc_32f_multiply_32fc(
            d_fft->get_inbuf(), signal, &d_window.front(), d_fft_len);
    } else {
        // don't window signal
        memcpy(d_fft->get_inbuf(), signal, sizeof(gr_complex) * d_fft_len);
    }

    d_fft->execute(); // fft

    // calc fft to periodogram
    volk_32fc_s32f_x2_power_spectral_density_32f(
        pxx, d_fft->get_outbuf(), d_fft_len, 1.0, d_fft_len);

    // do fftshift
    d_tmpbuflen = static_cast<unsigned int>(floor(d_fft_len / 2.0));
    memcpy(d_tmpbuf, &pxx[0], sizeof(float) * (d_tmpbuflen + 1));
    memcpy(&pxx[0], &pxx[d_fft_len - d_tmpbuflen], sizeof(float) * (d_tmpbuflen));
    memcpy(&pxx[d_tmpbuflen], d_tmpbuf, sizeof(float) * (d_tmpbuflen + 1));
}

// builds the frequency vector for periodogram
std::vector<float> signal_detector_cvf_impl::build_freq()
{
    std::vector<float> freq(d_fft_len);
    double point = -d_samp_rate / 2;
    for (unsigned int i = 0; i < d_fft_len; i++) {
        freq[i] = point;
        point += d_samp_rate / d_fft_len;
    }
    return freq;
}

// use firdes to get window coefficients
void signal_detector_cvf_impl::build_window()
{
    d_window.clear();
    if (d_window_type != gr::fft::window::win_type::WIN_NONE) {
        d_window = gr::filter::firdes::window(d_window_type, d_fft_len, 6.76);
    }
}

// set auto threshold by searching for jumps between bins
void signal_detector_cvf_impl::build_threshold()
{
    // copy array to work with
    memcpy(d_tmp_pxx, d_pxx_out, sizeof(float) * d_fft_len);
    // sort bins
    d_threshold = 500;
    std::sort(d_tmp_pxx, d_tmp_pxx + d_fft_len);
    float range = d_tmp_pxx[d_fft_len - 1] - d_tmp_pxx[0];
    // search specified normized jump
    for (unsigned int i = 0; i < d_fft_len; i++) {
        if ((d_tmp_pxx[i + 1] - d_tmp_pxx[i]) / range > 1 - d_sensitivity) {
            d_threshold = d_tmp_pxx[i];
            break;
        }
    }
}

// find bins above threshold and adjacent bins for each signal
// Datastructure: vector of the detected signals, each signal is a vector that is 2 elements long. The first element is the start bin, the second element is the end bin.
// Each Bin is stored as a pair of the bin number and the signal strength. 

std::vector<Detected_Signal> signal_detector_cvf_impl::find_signal_edges()
{
    Detected_Signal signal;
    std::vector<Detected_Signal> detected_signals;
    bool signal_started = false;
    int quantization = (int)floor(d_quantization * d_samp_rate);

    for (unsigned int i = 0; i < d_fft_len; i++) {
        //std::cout << "d_pxx_out[" << i << "] = " << d_pxx_out[i] << " Threshold: " << d_threshold << std::endl;
        

        if (d_pxx_out[i] > d_threshold) {
            if (!signal_started) {
                signal_started = true;
                signal.avg_rssi = d_pxx_out[i];
                signal.max_rssi = d_pxx_out[i];
                signal.min_rssi = d_pxx_out[i];
                signal.start_bin = i;
                signal.end_bin = i;
                signal.rssi.push_back(d_pxx_out[i]);
            } else {
                signal.avg_rssi = (signal.avg_rssi + d_pxx_out[i]) / 2;
                signal.rssi.push_back(d_pxx_out[i]);
                if (d_pxx_out[i] > signal.max_rssi) {
                    signal.max_rssi = d_pxx_out[i];
                }
                if (d_pxx_out[i] < signal.min_rssi) {
                    signal.min_rssi = d_pxx_out[i];
                }
                signal.end_bin = i;
            }

        } else{
            if (signal_started) {
                signal_started = false;

                double bandwidth = d_freq[signal.end_bin] - d_freq[signal.start_bin];
                signal.bandwidth = quantization * round(bandwidth / quantization);
                signal.center_freq = (d_freq[signal.start_bin] + d_freq[signal.end_bin]) / 2;
    
            
                detected_signals.push_back(signal);
            }
        }
    }

    // add last signal if it is still running
    if (signal_started) {
        signal_started = false;

                        double bandwidth = d_freq[signal.end_bin] - d_freq[signal.start_bin];
                signal.bandwidth = quantization * round(bandwidth / quantization);
                signal.center_freq = (d_freq[signal.start_bin] + d_freq[signal.end_bin]) / 2;
        detected_signals.push_back(signal);
    }
    return detected_signals;


/*
    std::vector<std::vector<std::pair<unsigned int, int>>> flanks;
    if (pos.size() == 0) {
        return flanks;
    }

    // check for adjacent bins to group
    std::vector<std::pair<unsigned int, int>> curr_edges;

    curr_edges.push_back(pos[0]); // first position is signal begin
    // check some special cases
    if (pos.size() == 0) {
        return flanks; // empty result
    } else if (pos.size() == 1) {
        curr_edges.push_back(pos[0]); // use same value for both flanks
        flanks.push_back(curr_edges);
    } else if (pos.size() == 2) {
        if (pos[0].first + 1 == pos[1].first) { // one signal with two bins
            curr_edges.push_back(pos[1]);
            flanks.push_back(curr_edges);
        } else { // two signals with one bin each
            curr_edges.push_back(pos[0]);
            flanks.push_back(curr_edges);
            curr_edges.clear();
            curr_edges.push_back(pos[1]);
            curr_edges.push_back(pos[1]);
            flanks.push_back(curr_edges);
        }
    } else {
        for (unsigned int i = 1; i < pos.size(); i++) {
            if (i == pos.size() - 1 && curr_edges.size() == 1 &&
                pos[i - 1].first + 1 == pos[i].first) {
                // write last flank
                curr_edges.push_back(pos[i]);
                flanks.push_back(curr_edges);
            } else {
                // if not adjacent bin, write new signal
                if (pos[i - 1].first + 1 != pos[i].first) {
                    curr_edges.push_back(pos[i - 1]);
                    flanks.push_back(curr_edges);
                    curr_edges.clear();
                    curr_edges.push_back(pos[i]);
                }
            }
        }
    }

    return flanks;*/
}


std::vector<Detected_Signal> signal_detector_cvf_impl::get_detected_signals()
{
    gr::thread::scoped_lock guard(d_mutex);
    //BOOST_LOG_TRIVIAL(info) << "get_detected_freqs" << std::endl;
    //BOOST_LOG_TRIVIAL(info) << "d_detected_freqs.size() = " << d_detected_freqs.size() << std::endl;
    std::vector<Detected_Signal> safe_version = d_detected_signals;
    return safe_version;
}


//</editor-fold>

//<editor-fold desc="GR Stuff">

int signal_detector_cvf_impl::work(int noutput_items,
                                   gr_vector_const_void_star& input_items,
                                   gr_vector_void_star& output_items)
{
    const gr_complex* in = (const gr_complex*)input_items[0];
    //float* out = (float*)output_items[0];

    periodogram(d_pxx, in);

    // averaging
    for (unsigned int i = 0; i < d_fft_len; i++) {
        d_pxx_out[i] = d_avg_filter[i].filter(d_pxx[i]);
    }

    if (d_auto_threshold) {
        build_threshold();
    }

    gr::thread::scoped_lock guard(d_mutex);
    d_detected_signals = find_signal_edges();
    //BOOST_LOG_TRIVIAL(info) << "d_detected_signals.size() = " << d_detected_signals.size() << std::endl;

    return 1; // one vector has been processed
}

//} /* namespace inspector */
//} /* namespace gr */