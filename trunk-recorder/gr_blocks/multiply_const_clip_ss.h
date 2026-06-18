#ifndef INCLUDED_GR_MULTIPLY_CONST_CLIP_SS_H
#define INCLUDED_GR_MULTIPLY_CONST_CLIP_SS_H

#include <gnuradio/blocks/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
namespace blocks {

class BLOCKS_API multiply_const_clip_ss : virtual public sync_block {
public:
#if GNURADIO_VERSION < 0x030900
  typedef boost::shared_ptr<multiply_const_clip_ss> sptr;
#else
  typedef std::shared_ptr<multiply_const_clip_ss> sptr;
#endif

  static sptr make(double k = 1.0);
  virtual void set_k(double k) = 0;
  virtual double k() const = 0;
};

} /* namespace blocks */
} /* namespace gr */

#endif /* INCLUDED_GR_MULTIPLY_CONST_CLIP_SS_H */
