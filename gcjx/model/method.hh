// Represent a method.

// Copyright (C) 2004, 2005 Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// gcjx is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// gcjx is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with gcjx; see the file COPYING.LIB.  If
// not, write to the Free Software Foundation, Inc.,
// 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#ifndef GCJX_MODEL_METHOD_HH
#define GCJX_MODEL_METHOD_HH

/// This enum is used by method invocation conversion.  Note that we
/// assume that there are no holes in the values here.
enum method_phase
{
  /// The style of Java 1.4 and earlier: no varargs, no boxing or
  /// unboxing.
  PHASE_1 = 0,

  /// Allows boxing and unboxing.
  PHASE_2 = 1,

  /// Allows varargs.
  PHASE_3 = 2,

  /// End marker.
  PHASE_TOO_FAR = 3
};

class model_method : public model_element, public IDeprecatable,
		     public ICatcher, public IAnnotatable,
		     public IModifiable, public IMember, public IScope
{
protected:

  enum resolution_state_value
  {
    NONE,
    CLASSES,
    RESOLVED
  };

  // Name.
  std::string name;

  // Signature.
  std::string descriptor;

  // Type parameters, or empty list if none.
  model_parameters type_parameters;

  // Formal parameters.
  std::list<ref_variable_decl> parameters;

  // 'throws' specification.
  model_throws_clause throw_decls;

  // Return type, or NULL for void or constructor.
  ref_forwarding_type return_type;

  // Body of method.
  ref_block body;

  // True if a varargs method.
  bool varargs;

  // True if this method was used.
  bool used;

  // True if this is an instance initializer method, aka 'finit$'.
  bool is_instance_initializer;

  // The resolution state.  We might be resolved multiple times, as
  // static methods are copied between different instantiations of a
  // class.
  resolution_state_value state;

  // We keep track of the end of the method as well as the beginning;
  // this is used by GCC for debugging information.
  location method_end;

  // The method this overrides, or NULL if none.
  model_method *override;

  // If this is a generic instantiation, this points to the parent
  // method.
  model_method *parent;

  // All generic instantiations of this method.
  model_instance_cache<model_method> instance_cache;

  void massage_modifiers (const ref_modifier_list &);
  bool return_type_substitutable_p (model_type *, model_type *, bool) const;
  model_method *do_method_conversion_p (const std::list<model_type *> &,
					method_phase);
  model_method *do_method_conversion_p (const model_type_map &,
					const std::list<model_type *> &,
					method_phase);

  annotation_kind get_annotation_kind () const
  {
    return ANNOTATE_METHOD;
  }

  // This constructor is used only when applying a type map.
  model_method (model_method *, const model_type_map &, model_class *);

  // This constructor is used only when creating the erasure of a
  // method.
  model_method (model_method *, model_class *);

public:

  model_method (const location &w, model_class *decl)
    : model_element (w),
      IMember (decl),
      varargs (false),
      used (false),
      is_instance_initializer (false),
      state (NONE),
      // By default we set the end location to the start location.
      method_end (w),
      override (NULL),
      parent (NULL)
  {
  }

  /// Create an abstract copy of this method.  This is used only when
  /// creating interface method copies of methods from Object.
  owner<model_method> create_abstract_instance ();

  /// Return true if this current method is a constructor, false
  /// otherwise.
  virtual bool constructor_p () const
  {
    return false;
  }

  /// Return true if this current method is a static initializer,
  /// false otherwise.
  bool static_initializer_p () const
  {
    return name == "<clinit>";
  }

  /// Return true if this current method is an instance initializer,
  /// false otherwise.
  bool instance_initializer_p () const
  {
    return is_instance_initializer;
  }

  /// Marks this method as an instance initializer.
  void set_instance_initializer ()
  {
    is_instance_initializer = true;
  }

  /// Return true if this method hides (for static methods) or
  /// overrides (for non-static methods) the method passed as an
  /// argument.  The second argument is the class "asking" this
  /// question; this is used to determine if this method is being
  /// declared or inherited.
  bool hides_or_overrides_p (model_method *, model_class *);

  /// Return true if this method's signature is a subsignature of
  /// other method's signature.  The second argument, if not NULL, is
  /// set to true if the signatures are identical.
  bool same_arguments_p (model_method *, bool * = NULL) const;

  void set_name (const std::string &n)
  {
    name = n;
  }

  std::string get_name () const
  {
    return name;
  }

  /// Tests whether this method has a different name than the other method,
  /// without requiring any string copying (which was a hotspot in profiles,
  /// thanks to model_class::method_inheritable_p).
  bool different_name_p (model_method *other) const
  {
    return (name != other->name);
  }

  /// Tests whether this method has the given name, without requiring any
  /// string copying (which was a minor hotspot in profiles, thanks to
  /// model_class::find_members).
  bool has_name_p (const std::string &other_name) const
  {
    return (name == other_name);
  }

  void set_body (const ref_block &b)
  {
    body = b;
  }

  ref_block get_body () const
  {
    assert (body);
    return body;
  }

  void set_parameters (const std::list<ref_variable_decl> &ps)
  {
    parameters = ps;
  }

  std::list<ref_variable_decl> get_parameters () const
  {
    return parameters;
  }

  int get_parameter_count () const
  {
    return parameters.size ();
  }

  void set_type_parameters (const std::list<ref_type_variable> &ts)
  {
    type_parameters.set_type_parameters (ts);
  }

  void set_type_parameters (const model_parameters &ts)
  {
    type_parameters = ts;
  }

  const model_parameters &get_type_parameters () const
  {
    return type_parameters;
  }

  void set_return_type (const ref_forwarding_type &t)
  {
    return_type = t;
  }

  model_type *get_return_type () const
  {
    return return_type->type ();
  }

  void set_varargs ()
  {
    varargs = true;
  }

  bool varargs_p () const
  {
    return varargs;
  }

  void set_throws (const std::list<ref_forwarding_type> &tlist)
  {
    throw_decls = tlist;
  }

  void set_throws (const model_throws_clause &ntc)
  {
    throw_decls = ntc;
  }

  /// Return true if the checked exception will be handled by our
  /// 'throws' clause.
  bool exception_handled_p (model_type *t)
  {
    return throw_decls.handled_p (t);
  }

  std::list<ref_forwarding_type> get_throws () const
  {
    return throw_decls.get ();
  }

  std::set<model_type *> get_throws_as_set () const
  {
    return throw_decls.get_as_set ();
  }

  // Return TRUE if this method is more specific than OTHER.
  bool more_specific_p (model_method *other);

  /// Return the method if arguments of the given types can be passed
  /// to this method.  The phase determines what kinds of conversions
  /// are considered.  The returned method might differ from 'this' if
  /// a generic instance is created.
  model_method *method_conversion_p (const std::list<model_type *> &,
				     model_type *assign_type,
				     method_phase);

  /// Like the above, but handles method conversion in the case where
  /// there are explicit type parameters to the invocation of a
  /// generic method.
  model_method *method_conversion_p (const std::list<model_class *> &,
				     const std::list<model_type *> &,
				     method_phase);

  /// Like the above, but wrap actual arguments in casts as
  /// appropriate.  Note that there is no phase argument here; when
  /// actually performing method conversion, we already know the
  /// method is applicable and we can apply all necessary conversions.
  void method_conversion (std::list<ref_expression> &);

  /// Check for potential applicability.
  bool potentially_applicable_p (const std::list<model_type *> &);

  /// Check for potential applicability, where explicit type
  /// parameters are given.
  bool potentially_applicable_p (const std::list<model_type *> &,
				 const std::list<ref_forwarding_type> &);

  /// Add an argument to a constructor.  This is only used for new
  /// hidden parameters, like captured 'final' local variables.
  virtual void add_parameter (const ref_variable_decl &)
  {
    // This is overridden by model_constructor; it should never be
    // called for ordinary methods.
    abort ();
  }

  /// Resolve the body of the method.
  virtual void resolve (resolution_scope *);

  /// Resolve the method's types: the return type, the argument types,
  /// and the types in the throws clause.
  virtual void resolve_classes (resolution_scope *);

  void note_throw_type (model_type *);

  void propagate_throws (resolution_scope *scope)
  {
    throw_decls.propagate_throws (scope);
  }

  /// If this method is deprecated, and deprecation warnings are
  /// enabled, issue a warning message.  The argument is the element
  /// requesting the check; any warning is issued against this
  /// element.
  void check_deprecated (const model_element *);

  /// Check whether this method was referenced and emit a warning if
  /// not.
  void check_referenced (resolution_scope *);

  /// Check whether this method satisfies the @Override rules.
  void check_override ();

  /// Indicate that this field has been referenced.
  void set_used ()
  {
    used = true;
  }

  std::string get_descriptor ();

  std::string get_pretty_name ();

  void visit (visitor *);

  /// This is called after code generation to clean up this method.
  /// This involves removing data which is not needed any more.  This
  /// will not remove anything that is visible from outside this
  /// method.
  void clean_up ();

  virtual model_method *apply_type_map (const model_type_map &, model_class *);

  virtual model_method *erasure (model_class *);

  static_result is_static_scope () const
  {
    return static_p () ? STATIC_CONTEXT : NOT_STATIC_CONTEXT;
  }

  std::string get_signature ();

  void check_definite_assignment ();

  void set_method_end (const location &w)
  {
    method_end = w;
  }

  location get_method_end () const
  {
    return method_end;
  }

  /// Return the return type of the method's erasure.  This is simpler
  /// than going via the erasure of the method, as no enclosing
  /// context is needed.
  model_type *get_erased_return_type () const;

  /// Return the method that this method overrides, or NULL if this
  /// method does not override another.
  model_method *get_override () const
  {
    return override;
  }

  /// Return the parent method if this method is a generic
  /// instantiation, or the method itself if not.
  model_method *get_parent ()
  {
    return parent ? parent : this;
  }
};

/// This represents a method that is the result of merging multiple
/// abstract methods together.  
class model_abstract_method : public model_method
{
  /// The original method which served as our template.
  model_method *original;

public:

  model_abstract_method (model_method *m)
    : model_method (m->get_location (), m->get_declaring_class ()),
      original (m)
  {
  }

  model_method *get_original () const
  {
    return original;
  }

  void visit (visitor *);
};

const format &operator% (const format &, model_method *);

#endif // GCJX_MODEL_METHOD_HH
