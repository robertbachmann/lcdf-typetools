#ifndef T1ITEM_HH
#define T1ITEM_HH
#include "t1cs.hh"
#include "vector.hh"
#include "permstr.hh"
class StringAccum;
class Type1Reader;
class Type1Writer;
class Type1Interp;
class Type1Font;
class Type1CopyItem;
class Type1Subr;
class Type1Definition;

class Type1Item {
  
  Type1Item(const Type1Item &);
  Type1Item &operator=(const Type1Item &);
  
 public:
  
  Type1Item()					{ }
  virtual ~Type1Item()				{ }
  
  virtual void gen(Type1Writer &) = 0;

  virtual Type1CopyItem *cast_copy()		{ return 0; }
  virtual Type1Subr *cast_subr()		{ return 0; }
  virtual Type1Definition *cast_definition()	{ return 0; }
  
};

class Type1CopyItem : public Type1Item {
  
  char *_value;
  int _length;
  
 public:
  
  Type1CopyItem(char *v, int l)		: _value(v), _length(l) { }
  ~Type1CopyItem()			{ delete[] _value; }

  char *value() const			{ return _value; }
  int length() const			{ return _length; }
  
  char *take_value();
  void set_value(char *, int);
  
  void gen(Type1Writer &);
  
  Type1CopyItem *cast_copy()		{ return this; }
  
};

class Type1EexecItem : public Type1Item {
  
  bool _eexec_on;
  
 public:
  
  Type1EexecItem(bool on)		: _eexec_on(on) { }
  
  void gen(Type1Writer &w);
  
};

class Type1Definition : public Type1Item {
  
  PermString _name;
  char *_val;
  PermString _definer;
  
  static int slurp_string(StringAccum &, int, Type1Reader *);
  static int slurp_proc(StringAccum &, int, Type1Reader *);
  
  void set_val(char *);
  void set_val(StringAccum &);
  void set_val_copy(const char *);
  
 public:
  
  typedef Vector<double> NumVector;
  
  ~Type1Definition();
  
  Type1Definition(PermString, char *, PermString);
  static Type1Definition *make(StringAccum &, Type1Reader * = 0,
			       bool force = false);
  
  PermString name() const		{ return _name; }
  const char *value() const		{ return _val; }
  PermString definer() const		{ return _definer; }
  
  bool value_bool(bool &) const;
  bool value_int(int &) const;
  bool value_num(double &) const;
  bool value_name(PermString &) const;
  bool value_numvec(NumVector &) const;
  bool value_numvec_vec(Vector<NumVector> &) const;
  bool value_normalize(Vector<NumVector> &in, Vector<NumVector> &out) const;
  bool value_namevec(Vector<PermString> &) const;
  
  void set_bool(bool);
  void set_int(int);
  void set_num(double);
  void set_name(PermString, bool name = true);
  void set_code(const char *s)		{ set_val_copy(s); }
  void set_numvec(const NumVector &, bool executable = false);
  void set_numvec_vec(const Vector<NumVector> &);
  void set_normalize(const Vector<NumVector> &, const Vector<NumVector> &);
  void set_namevec(const Vector<PermString> &, bool executable = true);
  
  void gen(Type1Writer &);
  void gen(StringAccum &);
  
  Type1Definition *cast_definition()	{ return this; }
  
};

class Type1Encoding : public Type1Item {
  
  PermString *_v;
  Type1Encoding *_copy_of;
  
  Type1Encoding(Type1Encoding *);
  
 public:
  
  Type1Encoding();
  ~Type1Encoding();
  
  PermString operator[](int e) const	{ assert(e>=0&&e<256); return _v[e]; }
  PermString elt(int e) const		{ return (*this)[e]; }
  PermString operator[](unsigned char e) const	{ return _v[e]; }
  PermString elt(unsigned char e) const		{ return operator[](e); }
  void put(int e, PermString p)		{ assert(e>=0&&e<256); _v[e] = p; }
  
  static Type1Encoding *standard_encoding();

  void gen(Type1Writer &);

};

class Type1Subr : public Type1Item {
  
  PermString _name;
  int _subrno;
  PermString _definer;
  
  Type1Charstring _cs;
  
  static PermString charstring_start;
  static int lenIV;
  static PermString cached_definer;
  
  Type1Subr(PermString, int, PermString, int, unsigned char *, int);
  Type1Subr(PermString, int, PermString, const Type1Charstring &);
  
 public:
  
  static Type1Subr *make(char *, int, int cs_start, int cs_len);
  static Type1Subr *make_subr(int, PermString, const Type1Charstring &);
  
  bool is_subr() const			{ return !_name; }
  PermString name() const		{ return _name; }
  int subrno() const			{ return _subrno; }
  PermString definer() const		{ return _definer; }
  
  Type1Charstring &t1cs()		{ return _cs; }
  const Type1Charstring &t1cs() const	{ return _cs; }
  operator Type1Charstring &()		{ return _cs; }
  operator const Type1Charstring &() const { return _cs; }
  
  bool run(Type1Interp &t1i)		{ return _cs.run(t1i); }
  
  static void set_charstring_definer(PermString);
  static void set_lenIV(int);
  
  void gen(Type1Writer &);
  
  virtual Type1Subr *cast_subr()	{ return this; }
  
};

class Type1SubrGroupItem : public Type1Item {
  
  Type1Font *_font;
  bool _is_subrs;
  char *_value;
  int _length;
  
 public:
  
  Type1SubrGroupItem(Type1Font *, bool, char *, int);
  ~Type1SubrGroupItem();
  
  void gen(Type1Writer &);
  
};

#endif
