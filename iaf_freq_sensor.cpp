/*
 *  iaf_freq_sensor.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "exceptions.h"
#include "iaf_freq_sensor.h"
#include "network.h"
#include "dict.h"
#include "integerdatum.h"
#include "doubledatum.h"
#include "dictutils.h"
#include "numerics.h"
#include "universal_data_logger_impl.h"

#include <limits>

nest::RecordablesMap<mynest::iaf_freq_sensor> mynest::iaf_freq_sensor::recordablesMap_;
using namespace nest;

namespace nest
{

  /*
   * Override the create() method with one call to RecordablesMap::insert_()
   * for each quantity to be recorded.
   */
  template <>
  void RecordablesMap<mynest::iaf_freq_sensor>::create()
  {
    // use standard names whereever you can for consistency!
    insert_(names::V_m, &mynest::iaf_freq_sensor::get_V_m_);
    insert_("Currents", &mynest::iaf_freq_sensor::get_y0_);
    insert_("Syn",      &mynest::iaf_freq_sensor::get_y1_);
    insert_("V",      &mynest::iaf_freq_sensor::get_y2_);
    insert_("Im",      &mynest::iaf_freq_sensor::get_currents_);
  }
}

namespace mynest
{
  /* ----------------------------------------------------------------
   * Default constructors defining default parameters and state
   * ---------------------------------------------------------------- */

  mynest::iaf_freq_sensor::Parameters_::Parameters_()
    : Tau_       (  30.0   ),  // ms
      C_         (   1.0   ),  // pF
      TauR_      (   2.0   ),  // ms
      U0_        (   0.0   ),  // mV
      I_e_       (   0.0   ),  // pA
      V_reset_   ( -10.0   ),  // mV, rel to U0_
      Theta_     (  -1.0   ),  // mV, rel to U0_
      LowerBound_(-std::numeric_limits<double_t>::infinity()),
      Sigma_     (  30.0   ),
      Ti_        (  50.0   ),   // ms
      num_of_receptors_ ( 2 )
  {}

  mynest::iaf_freq_sensor::State_::State_()
    : y0_   (0.0),
      y1_   (0.0),
      y2_   (0.0),
      y3_   (0.0),
      currents_ (0.0),
      ti_   (-std::numeric_limits<double_t>::infinity()),
      r_    (0)
  {}

  /* ----------------------------------------------------------------
   * Parameter and state extractions and manipulation functions
   * ---------------------------------------------------------------- */

  void mynest::iaf_freq_sensor::Parameters_::get(DictionaryDatum &d) const
  {
    def<double>(d, names::E_L, U0_);   // Resting potential
    def<double>(d, names::I_e, I_e_);
    def<double>(d, names::V_th, Theta_+U0_); // threshold value
    def<double>(d, names::V_reset, V_reset_+U0_);
    def<double>(d, names::V_min, LowerBound_+U0_);
    def<double>(d, names::C_m, C_);
    def<double>(d, names::tau_m, Tau_);
    def<double>(d, names::t_ref, TauR_);
    def<double>(d, "Sigma", Sigma_);
    def<double>(d, "Ti", Ti_);
    def<int>(d, "n_synapses", num_of_receptors_);
  }

  double mynest::iaf_freq_sensor::Parameters_::set(const DictionaryDatum& d)
  {
    // if U0_ is changed, we need to adjust all variables defined relative to U0_
    const double ELold = U0_;
    updateValue<double>(d, names::E_L, U0_);
    const double delta_EL = U0_ - ELold;

    updateValue<double>(d, names::V_reset, V_reset_);
    updateValue<double>(d, names::V_th, Theta_);
    updateValue<double>(d, names::V_min, LowerBound_);

    updateValue<double>(d, names::I_e, I_e_);
    updateValue<double>(d, names::C_m, C_);
    updateValue<double>(d, names::tau_m, Tau_);
    updateValue<double>(d, names::t_ref, TauR_);
    updateValue<double>(d, "Sigma", Sigma_);
    updateValue<double>(d, "Ti", Ti_);


    if ( C_ <= 0.0 )
      throw BadProperty("Capacitance must be > 0.");

    if ( Tau_ <= 0.0 )
      throw BadProperty("Membrane time constant must be > 0.");

    if ( Ti_ <= 0.0 )
        throw BadProperty("Integration time must be > 0.");

    if ( TauR_ < 0.0 )
    	throw BadProperty("The refractory time t_ref can't be negative.");

    return delta_EL;
  }

  void mynest::iaf_freq_sensor::State_::get(DictionaryDatum &d, const Parameters_& p) const
  {
    def<double>(d, names::V_m, y3_); // Membrane potential
  }

  void mynest::iaf_freq_sensor::State_::set(const DictionaryDatum& d, const Parameters_& p, double delta_EL)
  {
    updateValue<double>(d, names::V_m, y3_);
  }

  mynest::iaf_freq_sensor::Buffers_::Buffers_(iaf_freq_sensor& n)
    : logger_(n)
  {}

  mynest::iaf_freq_sensor::Buffers_::Buffers_(const Buffers_ &, iaf_freq_sensor& n)
    : logger_(n)
  {}


  /* ----------------------------------------------------------------
   * Default and copy constructor for node
   * ---------------------------------------------------------------- */

  mynest::iaf_freq_sensor::iaf_freq_sensor()
    : Archiving_Node(),
      P_(),
      S_(),
      B_(*this)
  {
    recordablesMap_.create();
  }

  mynest::iaf_freq_sensor::iaf_freq_sensor(const iaf_freq_sensor& n)
    : Archiving_Node(n),
      P_(n.P_),
      S_(n.S_),
      B_(n.B_, *this)
  {}

  /* ----------------------------------------------------------------
   * Node initialization functions
   * ---------------------------------------------------------------- */

  void mynest::iaf_freq_sensor::init_state_(const Node& proto)
  {
    const iaf_freq_sensor& pr = downcast<iaf_freq_sensor>(proto);
    S_ = pr.S_;
  }

  void mynest::iaf_freq_sensor::init_buffers_()
  {
    B_.spikes_.clear();       // includes resize
    B_.currents_.clear();        // includes resize

    B_.logger_.reset();

    Archiving_Node::clear_history();
  }

  void mynest::iaf_freq_sensor::calibrate()
  {
    B_.logger_.init();  // ensures initialization in case mm connected after Simulate

    const double h = Time::get_resolution().get_ms();

    P_.receptor_types_.resize(P_.num_of_receptors_);
    B_.spikes_.resize(P_.num_of_receptors_);
    for (size_t i = 0; i < P_.num_of_receptors_; i++)
    {
        P_.receptor_types_[i] = i+1;
        B_.spikes_[i].resize();
    }

    // these P are independent
    V_.P21_ = 2.0 / (std::sqrt(3.0*P_.Sigma_) * std::pow(numerics::pi,0.25) * P_.Sigma_ );
    V_.P22_ = - 1.0 / (P_.Sigma_ * P_.Sigma_);

    V_.P33_ = std::exp(-h/P_.Tau_);
    V_.P31_ = P_.Tau_ /P_.C_ * (1.0 - V_.P33_);

    // TauR specifies the length of the absolute refractory period as
    // a double_t in ms. The grid based iaf_freq_sensor can only handle refractory
    // periods that are integer multiples of the computation step size (h).
    // To ensure consistency with the overall simulation scheme such conversion
    // should be carried out via objects of class nest::Time. The conversion
    // requires 2 steps:
    //     1. A time object is constructed defining representation of
    //        TauR in tics. This representation is then converted to computation time
    //        steps again by a strategy defined by class nest::Time.
    //     2. The refractory time in units of steps is read out get_steps(), a member
    //        function of class nest::Time.
    //
    // The definition of the refractory period of the iaf_freq_sensor is consistent
    // the one of iaf_freq_sensor_ps.
    //
    // Choosing a TauR that is not an integer multiple of the computation time
    // step h will lead to accurate (up to the resolution h) and self-consistent
    // results. However, a neuron model capable of operating with real valued spike
    // time may exhibit a different effective refractory time.

    V_.RefractoryCounts_ = Time(Time::ms(P_.TauR_)).get_steps();
    assert(V_.RefractoryCounts_ >= 0);  // since t_ref_ >= 0, this can only fail in error
  }

  /* ----------------------------------------------------------------
   * Update and spike handling functions
   */

  inline
  void mynest::iaf_freq_sensor::update_currents_(const double_t t)
  {
      double tt_s = t - S_.ti_-P_.Ti_/2.0;
      tt_s = V_.P22_ * tt_s * tt_s;
      S_.currents_ = S_.y0_ * V_.P21_ * (1.0 + tt_s)*std::exp(tt_s/2.0);
  }

  void mynest::iaf_freq_sensor::update(Time const & origin, const long_t from, const long_t to)
  {
    assert(to >= 0 && (delay) from < Scheduler::get_min_delay());
    assert(from < to);

    double dt,t;
    const double h = Time::get_resolution().get_ms();
    double Vm0;

    double dk;
    double t_h,v_h;

    for ( long_t lag = from ; lag < to ; ++lag )
    {
      t = Time(Time::step(origin.get_steps()+lag+1)).get_ms();
      if(B_.spikes_[0].get_value(lag) > 0.1)
      {
          // Integration spike arrive 
          S_.y1_ = 0.0;
          S_.y2_ = 0.0;
          S_.currents_ = 0.0;
          S_.ti_ = t;
          //S_.y3_ = P_.V_reset_;
          //S_.y1_ = 1.0;
      }
      if(B_.spikes_[1].get_value(lag) > 0.1)
      {
          // Encoding spike arrive 
          //S_.y2_ = 0.0;
          //S_.currents_ = 0.0;
          //S_.ti_ = t;
          S_.y3_ = P_.V_reset_;
          S_.y1_ = 1.0;
      }
      Vm0 = S_.y3_;

      if ( S_.r_ == 0 )
      {
        // neuron not refractory
        S_.y3_ = V_.P33_ * S_.y3_ + V_.P31_ * S_.y1_ * std::abs(S_.y2_);
        S_.y1_ = V_.P33_ * S_.y1_;

        //Simpson's method for v integration
        dk = S_.currents_;
        update_currents_(t+h/2.0);
        dk += 4.0 * S_.currents_;
        update_currents_(t+h);
        dk += S_.currents_;
        S_.y2_ += dk * h / 6.0;

        // lower bound of membrane potential
        S_.y3_ = ( S_.y3_ < P_.LowerBound_ ? P_.LowerBound_ : S_.y3_);
        S_.y2_ = ( S_.y2_ < P_.LowerBound_ ? P_.LowerBound_ : S_.y2_);
        S_.y1_ = ( S_.y1_ < P_.LowerBound_ ? P_.LowerBound_ : S_.y1_);

      }
      else // neuron is absolute refractory
        --S_.r_;

      if ( Vm0 < P_.Theta_ && S_.y3_ >= P_.Theta_)
      {
        S_.r_  = V_.RefractoryCounts_;
        S_.y3_ = P_.U0_;
        S_.y2_ = 0.0;
        S_.y1_ = 0.0;
        // A supra-threshold membrane potential should never be observable.
        // The reset at the time of threshold crossing enables accurate integration
        // independent of the computation step size, see [2,3] for details.

        set_spiketime(Time::step(origin.get_steps()+lag+1));
        SpikeEvent se;
        network()->send(*this, se, lag);
      }

      // set new input current
      S_.y0_ = B_.currents_.get_value(lag);

      // log state data
      B_.logger_.record_data(origin.get_steps() + lag);
    }
  }

  port mynest::iaf_freq_sensor::connect_sender(SpikeEvent&, port receptor_type)
  {
      if (receptor_type <=0 || receptor_type > static_cast <port>(P_.num_of_receptors_))
          throw IncompatibleReceptorType(receptor_type, get_name(), "SpikeEvent");
      return receptor_type;
  }

  void mynest::iaf_freq_sensor::handle(SpikeEvent& e)
  {
    assert(e.get_delay() > 0);

    for (size_t i=0; i < P_.num_of_receptors_; ++i)
    {
      if (P_.receptor_types_[i] == e.get_rport()){
        B_.spikes_[i].add_value(e.get_rel_delivery_steps(network()->get_slice_origin()),
                             e.get_weight() * e.get_multiplicity());
      }
    }
  }

  void mynest::iaf_freq_sensor::handle(CurrentEvent& e)
  {
    assert(e.get_delay() > 0);

    const double_t I = e.get_current();
    const double_t w = e.get_weight();

    B_.currents_.add_value(e.get_rel_delivery_steps(network()->get_slice_origin()), w * I);
  }

  void mynest::iaf_freq_sensor::handle(DataLoggingRequest& e)
  {
    B_.logger_.handle(e);
  }

} // namespace
