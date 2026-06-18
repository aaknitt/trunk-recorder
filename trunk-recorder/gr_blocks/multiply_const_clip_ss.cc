#include "multiply_const_clip_ss.h"

#include <cmath>
#include <gnuradio/io_signature.h>

namespace gr {
namespace blocks {

class multiply_const_clip_ss_impl : public multiply_const_clip_ss {
public:
#if GNURADIO_VERSION < 0x030900
  typedef boost::shared_ptr<multiply_const_clip_ss_impl> sptr;
#else
  typedef std::shared_ptr<multiply_const_clip_ss_impl> sptr;
#endif

  static sptr make(double k) { return gnuradio::get_initial_sptr(new multiply_const_clip_ss_impl(k)); }

  multiply_const_clip_ss_impl(double k)
      : sync_block("multiply_const_clip_ss",
                   io_signature::make(1, 1, sizeof(int16_t)),
                   io_signature::make(1, 1, sizeof(int16_t))),
        d_k(k) {}

  void set_k(double k) override { d_k = k; }

  double k() const override { return d_k; }

  int work(int noutput_items,
           gr_vector_const_void_star &input_items,
           gr_vector_void_star &output_items) override {
    const int16_t *in = (const int16_t *)input_items[0];
    int16_t *out = (int16_t *)output_items[0];

    for (int i = 0; i < noutput_items; i++) {
      const double product = in[i] * d_k;
      if (product > 32767.0) {
        out[i] = 32767;
      } else if (product < -32768.0) {
        out[i] = -32768;
      } else {
        out[i] = (int16_t)std::lround(product);
      }
    }

    return noutput_items;
  }

private:
  double d_k;
};

multiply_const_clip_ss::sptr multiply_const_clip_ss::make(double k) {
  return multiply_const_clip_ss_impl::make(k);
}

} /* namespace blocks */
} /* namespace gr */
