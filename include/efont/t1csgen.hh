#ifndef LIBEFONT_T1CSGEN_HH
#define LIBEFONT_T1CSGEN_HH
#include "t1interp.hh"
#include "straccum.hh"

class Type1CharstringGen { public:

    Type1CharstringGen(int precision = 5);

    void clear();
    char *data() const			{ return _ncs.data(); }
    int length() const			{ return _ncs.length(); }

    void gen_number(double, int = 0);
    void gen_command(int);
    void gen_stack(CharstringInterp &, int for_cmd);

    void append_charstring(const String &);

    double current_point_x(bool real) const { return (real ? _true_x : _false_x); }
    double current_point_y(bool real) const { return (real ? _true_y : _false_y); }

    Type1Charstring *output();
    void output(Type1Charstring &);

    static void assign_substring_callsubr(Type1Charstring &, int pos, int len, int subr);
    
  private:
  
    StringAccum _ncs;
    int _precision;
    double _f_precision;
    
    double _true_x;
    double _true_y;
    double _false_x;
    double _false_y;
  
};

#endif
