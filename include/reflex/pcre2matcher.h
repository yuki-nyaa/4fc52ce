/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      pcre2matcher.h
@brief     PCRE2-JIT-based matcher engines for pattern matching
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_PCRE2MATCHER_H
#define REFLEX_PCRE2MATCHER_H

#include <reflex/absmatcher.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

namespace reflex {

/// PCRE2 JIT-optimized matcher engine class implements reflex::PatternMatcher pattern matching interface with scan, find, split functors and iterators, using the PCRE2 library.
class PCRE2Matcher : public PatternMatcher<std::string> {
 public:
  /// Convert a regex to an acceptable form, given the specified regex library signature `"[decls:]escapes[?+]"`, see reflex::convert.
  template<typename T>
  static std::string convert(T regex, convert_flag_type flags = convert_flag::none)
  {
    return reflex::convert(regex, "imPRsx!#<=&:abcdefghlnrstuvwxzABDGHKLNQRSUWXZ0123456789?+", flags);
  }
  /// Default constructor.
  PCRE2Matcher()
    :
      PatternMatcher<std::string>(),
      opc_(nullptr),
      dat_(nullptr),
      ctx_(nullptr),
      stk_(nullptr)
  {
    reset();
  }
  /// Construct matcher engine from a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a string regex
  PCRE2Matcher(
      const P     *pattern,         ///< points to a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = nullptr,      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
      uint32_t     options = 0)     ///< pcre2_compile() options
    :
      PatternMatcher<std::string>(pattern, input, opt),
      cop_(options),
      opc_(nullptr),
      dat_(nullptr),
      ctx_(nullptr),
      stk_(nullptr)
  {
    reset();
    compile();
  }
  /// Construct matcher engine from a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a string regex
  PCRE2Matcher(
      const P&     pattern,         ///< a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = nullptr,      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
      uint32_t     options = 0)     ///< pcre2_compile() options
    :
      PatternMatcher<std::string>(pattern, input, opt),
      cop_(options),
      opc_(nullptr),
      dat_(nullptr),
      ctx_(nullptr),
      stk_(nullptr)
  {
    reset();
    compile();
  }
  /// Copy constructor.
  PCRE2Matcher(const PCRE2Matcher& matcher) ///< matcher to copy
    :
      PatternMatcher<std::string>(matcher),
      cop_(matcher.cop_),
      flg_(matcher.flg_),
      opc_(nullptr),
      dat_(nullptr),
      ctx_(nullptr),
      stk_(nullptr)
  {
    reset();
    cop_ = matcher.cop_;
    flg_ = matcher.flg_;
#ifdef pcre2_code_copy_with_tables
    opc_ = pcre2_code_copy_with_tables(matcher.opc_);
    dat_ = pcre2_match_data_create_from_pattern(opc_, nullptr);
    jit_ = matcher.jit_ && pcre2_jit_compile(opc_, PCRE2_JIT_PARTIAL_HARD) == 0 && pcre2_pattern_info(opc_, PCRE2_INFO_JITSIZE, nullptr) != 0;
#else
    compile();
#endif
  }
  /// Delete matcher.
  virtual ~PCRE2Matcher()
  {
    if (stk_ != nullptr)
      pcre2_jit_stack_free(stk_);
    if (ctx_ != nullptr)
      pcre2_match_context_free(ctx_);
    if (dat_ != nullptr)
      pcre2_match_data_free(dat_);
    if (opc_ != nullptr)
      pcre2_code_free(opc_);
  }
  /// Assign a matcher.
  PCRE2Matcher& operator=(const PCRE2Matcher& matcher) ///< matcher to copy
  {
    PatternMatcher<std::string>::operator=(matcher);
    pattern(matcher);
    return *this;
  }
  /// Polymorphic cloning.
  virtual PCRE2Matcher *clone()
  {
    return new PCRE2Matcher(*this);
  }
  /// Reset this matcher's state to the initial state and when assigned new input.
  virtual void reset(const char *opt = nullptr)
  {
    DBGLOG("PCRE2Matcher::reset()");
    flg_ = 0;
    grp_ = 0;
    PatternMatcher::reset(opt);
    if (ctx_ == nullptr)
      ctx_ = pcre2_match_context_create(nullptr);
    if (ctx_ != nullptr && stk_ == nullptr)
    {
      stk_ = pcre2_jit_stack_create(32*1024, 512*1024, nullptr);
      pcre2_jit_stack_assign(ctx_, nullptr, stk_);
    }
  }
  using PatternMatcher::pattern;
  /// Set the pattern to use with this matcher as a shared pointer to another matcher pattern.
  virtual PatternMatcher& pattern(const PCRE2Matcher& matcher) ///< the other matcher
    /// @returns this matcher.
  {
    PatternMatcher<std::string>::pattern(matcher);
    cop_ = matcher.cop_;
    flg_ = matcher.flg_;
#ifdef pcre2_code_copy_with_tables
    if (dat_ != nullptr)
    {
      pcre2_match_data_free(dat_);
      dat_ = nullptr;
    }
    if (opc_ != nullptr)
    {
      pcre2_code_free(opc_);
      opc_ = nullptr;
    }
    opc_ = pcre2_code_copy_with_tables(matcher.opc_);
    dat_ = pcre2_match_data_create_from_pattern(opc_, nullptr);
    jit_ = matcher.jit_ && pcre2_jit_compile(opc_, PCRE2_JIT_PARTIAL_HARD) == 0 && pcre2_pattern_info(opc_, PCRE2_INFO_JITSIZE, nullptr) != 0;
#else
    compile();
#endif
    return *this;
  }
  /// Set the pattern regex string to use with this matcher (the given pattern is shared and must be persistent).
  virtual PatternMatcher& pattern(const Pattern *pattern) ///< pointer to a regex string
    /// @returns this matcher.
  {
    PatternMatcher::pattern(pattern);
    compile();
    return *this;
  }
  /// Set the pattern regex string to use with this matcher.
  virtual PatternMatcher& pattern(const char *pattern) ///< regex string
    /// @returns this matcher.
  {
    PatternMatcher::pattern(pattern);
    compile();
    return *this;
  }
  /// Set the pattern regex string to use with this matcher.
  virtual PatternMatcher& pattern(const std::string& pattern) ///< regex string
    /// @returns this matcher.
  {
    PatternMatcher::pattern(pattern);
    compile();
    return *this;
  }
  /// Returns a pair of pointer and length of the captured match for n > 0 capture index or <text(),size() for n == 0.
  virtual std::pair<const char*,size_t> operator[](size_t n) ///< nth capture index > 0 or 0
    /// @returns pair.
    const
  {
    if (n == 0)
      return std::pair<const char*,size_t>(txt_, len_);
    if (dat_ == nullptr || n >= pcre2_get_ovector_count(dat_))
      return std::pair<const char*,size_t>(nullptr, 0);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(dat_);
    size_t n2 = 2 * n;
    if (ovector[n2] == PCRE2_UNSET)
      return std::pair<const char*,size_t>(nullptr, 0);
    return std::pair<const char*,size_t>(buf_ + ovector[n2], ovector[n2 + 1] - ovector[n2]);
  }
  /// Returns the group capture identifier containing the group capture index >0 and name (or nullptr) of a named group capture, or (1,nullptr) by default
  virtual std::pair<size_t,const char*> group_id()
    /// @returns a pair of size_t and string
  {
    grp_ = 1;
    if (dat_ == nullptr || pcre2_get_ovector_count(dat_) <= 1)
      return std::pair<size_t,const char*>(0, nullptr);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(dat_);
    if (ovector[2] == PCRE2_UNSET)
      return group_next_id();
    return id();
  }
  /// Returns the next group capture identifier containing the group capture index >0 and name (or nullptr) of a named group capture, or (0,nullptr) when no more groups matched
  virtual std::pair<size_t,const char*> group_next_id()
    /// @returns a pair of size_t and string
  {
    if (dat_ == nullptr)
      return std::pair<size_t,const char*>(0, nullptr);
    PCRE2_SIZE n = pcre2_get_ovector_count(dat_);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(dat_);
    while (++grp_ < n)
      if (ovector[2 * grp_] != PCRE2_UNSET)
        break;
    if (grp_ >= n)
      return std::pair<size_t,const char*>(0, nullptr);
    return id();
  }
 protected:
  /// Translate group capture index to id pair (index,name)
  std::pair<size_t,const char*> id()
  {
    PCRE2_SIZE name_count = 0;;
    PCRE2_SPTR name_table = nullptr;
    PCRE2_SIZE name_entry_size = 0;
    (void)pcre2_pattern_info(opc_, PCRE2_INFO_NAMECOUNT, &name_count);
    (void)pcre2_pattern_info(opc_, PCRE2_INFO_NAMETABLE, &name_table);
    (void)pcre2_pattern_info(opc_, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
    if (name_table != nullptr)
    {
      while (name_count > 0)
      {
        --name_count;
        PCRE2_SPTR p = name_table + name_count * name_entry_size;
        if ((static_cast<size_t>(static_cast<uint8_t>(p[0]) << 8) | static_cast<uint8_t>(p[1])) == grp_)
          return std::pair<size_t,const char*>(grp_, reinterpret_cast<const char*>(p + 2));
      }
    }
    return std::pair<size_t,const char*>(grp_, nullptr);
  }
  /// Compile pattern for jit partial matching and allocate match data.
  void compile()
  {
    DBGLOG("BEGIN PCRE2Matcher::compile()");
    if (dat_ != nullptr)
    {
      pcre2_match_data_free(dat_);
      dat_ = nullptr;
    }
    if (opc_ != nullptr)
    {
      pcre2_code_free(opc_);
      opc_ = nullptr;
    }
    int err;
    PCRE2_SIZE pos;
    ASSERT(pat_ != nullptr);
    opc_ = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat_->c_str()), static_cast<PCRE2_SIZE>(pat_->size()), cop_, &err, &pos, nullptr);
    if (opc_ == nullptr)
    {
      PCRE2_UCHAR message[120];
      pcre2_get_error_message(err, message, sizeof(message));
      throw regex_error(reinterpret_cast<char*>(message), *pat_, pos);
    }
    jit_ = pcre2_jit_compile(opc_, PCRE2_JIT_PARTIAL_HARD) == 0 && pcre2_pattern_info(opc_, PCRE2_INFO_JITSIZE, nullptr) != 0;
    dat_ = pcre2_match_data_create_from_pattern(opc_, nullptr);
    DBGLOGN("jit=%d", jit_);
  }
  /// The match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH, implemented with PCRE2.
  virtual size_t match(Method method) ///< match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns nonzero when input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
  {
    DBGLOG("BEGIN PCRE2Matcher::match(%d)", method);
    reset_text();
    txt_ = buf_ + cur_; // set first of text(), cur_ was last pos_, or cur_ was set with more()
    cur_ = pos_;
    if (next_match(method))
    {
      if (method == Const::SPLIT)
      {
        DBGLOGN("Split match");
        len_ = cur_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
        if (cur_ == pos_ && at_bob() && at_end())
        {
          cap_ = Const::EMPTY;
          got_ = Const::EOB;
        }
        else
        {
          set_current(pos_);
        }
        DBGLOGN("Split: act = %zu txt = '%s' len = %zu pos = %zu", cap_, txt_, len_, pos_);
        DBGLOG("END PCRE2Matcher::match()");
        return cap_;
      }
      if (method == Const::FIND)
        txt_ = buf_ + cur_;
      set_current(pos_);
      len_ = cur_ - (txt_ - buf_);
      if (len_ == 0 && cap_ != 0 && opt_.N && at_end())
        cap_ = 0;
      DBGLOGN("Accept: act = %zu txt = '%s' len = %zu", cap_, txt_, len_);
      DBGCHK(len_ != 0 || method == Const::MATCH || (method == Const::FIND && opt_.N));
      DBGLOG("END PCRE2Matcher::match()");
      return cap_;
    }
    cap_ = 0;
    if (method == Const::SPLIT)
    {
      if (got_ != Const::EOB)
        cap_ = Const::EMPTY;
      flg_ |= PCRE2_NOTEMPTY_ATSTART;
      set_current(end_);
      got_ = Const::EOB;
      len_ = cur_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
      DBGLOGN("Split: act = %zu txt = '%s' len = %zu pos = %zu", cap_, txt_, len_, pos_);
      DBGLOG("END PCRE2Matcher::match()");
      return cap_;
    }
    len_ = 0;
    DBGLOGN("No match, pos = %zu", pos_);
    DBGLOG("END PCRE2Matcher::match()");
    return cap_;
  }
  /// Perform next PCRE2 match, return true if a match is found.
  bool next_match(Method method) ///< match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns true when PCRE2 match found
  {
    if (pos_ == end_ && !eof_)
      (void)peek_more();
    uint32_t flg = flg_;
    if (!eof_)
      flg |= PCRE2_PARTIAL_HARD;
    if (!at_bol())
      flg |= PCRE2_NOTBOL;
    if (method == Const::SCAN || (method == Const::FIND && !opt_.N))
      flg |= PCRE2_NOTEMPTY;
    else if (method == Const::FIND || method == Const::SPLIT)
      flg_ &= ~(PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED);
    while (true)
    {
      DBGLOGN("pcre2_match() pos = %zu end = %zu", pos_, end_);
      int rc;
      /* TODO this may be a bug in PCRE2? If JIT is used to compile the
         pattern for complete and partial matching (these are not exclusive):
         pcre2_jit_compile(opc_, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_HARD);
         then pcre2_jit_match() without option PCRE2_PARTIAL_HARD always
         returns error PCRE2_ERROR_JIT_BADOPTION when we actually want the
         complete match when the end of the input is reached.  A final complete
         match is required, because we cannot tell whether the final partial
         match is also a complete match.  Therefore, pcre2_jit_match() is
         unusable and disabled below in favor of pcre2_match().
         */
#if 0
      if (jit_ && !(flg & PCRE2_ANCHORED))
        rc = pcre2_jit_match(opc_, reinterpret_cast<PCRE2_SPTR>(buf_), end_, pos_, flg, dat_, ctx_);
      else
#endif
        rc = pcre2_match(opc_, reinterpret_cast<PCRE2_SPTR>(buf_), end_, pos_, flg, dat_, ctx_);
      if (rc > 0)
      {
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(dat_);
        cur_ = ovector[0];
        if (method == Const::FIND || method == Const::SPLIT || pos_ == cur_)
        {
          pos_ = ovector[1];
          if (cur_ == pos_ && (method == Const::FIND || method == Const::SPLIT))
            flg_ |= PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
          for (cap_ = 1; cap_ < static_cast<size_t>(rc) && ovector[2*cap_] == PCRE2_UNSET; ++cap_)
            continue;
          return true;
        }
        rc = PCRE2_ERROR_NOMATCH; // SCAN and MATCH must be anchored (avoid PCRE2_ANCHORED that disables JIT)
      }
      if (rc == PCRE2_ERROR_PARTIAL)
      {
        if (method == Const::FIND)
          txt_ = buf_ + pos_;
        cur_ = pos_;
        pos_ = end_;
        if (peek_more() == EOF)
        {
          if ((flg & PCRE2_PARTIAL_HARD) == 0)
            return false;
          flg &= ~PCRE2_PARTIAL_HARD;
        }
        pos_ = cur_;
      }
      else if (rc == PCRE2_ERROR_NOMATCH && (method == Const::FIND || method == Const::SPLIT))
      {
        if ((flg & PCRE2_NOTEMPTY_ATSTART) != 0)
        {
          if (at_end())
            return false;
          flg &= ~(PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED);
          ++pos_;
          continue;
        }
        if (method == Const::FIND)
          txt_ = buf_ + end_;
        pos_ = end_;
        if (peek_more() == EOF)
          return false;
      }
      else
      {
#if defined(DEBUG_REFLEX)
        PCRE2_UCHAR message[120];
        pcre2_get_error_message(rc, message, sizeof(message));
        DBGLOGN("Return code: %d '%s' flg=%x", rc, message, flg);
#endif
        return false;
      }
    }
  }
  uint32_t             cop_; ///< PCRE2 compiled options
  uint32_t             flg_; ///< PCRE2 match flags
  pcre2_code          *opc_; ///< compiled PCRE2 code
  pcre2_match_data    *dat_; ///< PCRE2 match data
  pcre2_match_context *ctx_; ///< PCRE2 match context;
  pcre2_jit_stack     *stk_; ///< PCRE2 jit match stack
  PCRE2_SIZE           grp_; ///< last index for group_next_id()
  bool                 jit_; ///< true if jit-compiled PCRE2 code
};

/// PCRE2 JIT-optimized native PCRE2_UTF+PCRE2_UCP matcher engine class, extends PCRE2Matcher.
class PCRE2UTFMatcher : public PCRE2Matcher {
 public:
  /// Convert a regex to an acceptable form, given the specified regex library signature `"[decls:]escapes[?+]"`, see reflex::convert.
  template<typename T>
  static std::string convert(T regex, convert_flag_type flags = convert_flag::none)
  {
    return reflex::convert(regex, "imsx!#<=:abcdefghlnprstuvwxzABDGHKLNPQRSUWXZ0123456789?+", flags);
  }
  /// Default constructor.
  PCRE2UTFMatcher() : PCRE2Matcher()
  { }
  /// Construct matcher engine from a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a string regex
  PCRE2UTFMatcher(
      const P     *pattern,         ///< points to a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = nullptr)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PCRE2Matcher(pattern, input, opt, PCRE2_UTF | PCRE2_UCP)
  { }
  /// Construct matcher engine from a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a string regex
  PCRE2UTFMatcher(
      const P&     pattern,         ///< a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = nullptr)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PCRE2Matcher(pattern, input, opt, PCRE2_UTF | PCRE2_UCP)
  { }
};

} // namespace reflex

#endif
