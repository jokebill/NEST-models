/*
 *  stdp_connection_multi.h
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

#ifndef STDP_CONNECTION_MULTI_H
#define STDP_CONNECTION_MULTI_H

/* BeginDocumentation
  Name: stdp_synapse_multi - Synapse type for spike-timing dependent
   plasticity with linear multiplicative rule for LTD and exponential
   multiplicative rule for LTP.

  Description:
   stdp_synapse_multi is a connector to create synapses with spike time 
   dependent plasticity (as defined in [1]). Here the weight dependence
   exponent can be set separately for potentiation and depression.

  Parameters:
   Aplus      double - Amplitude of potentiation
   Aneg       double - Amplitude of depression
   tplus      double - Time constant for potentiation
   tneg       double - Time constant for depression
   Wmax       double - Maximum limitation of synapse weight
   Esyn       double - Multiplication to w when sending spikes
   EmitSpk    bool   - whether to emit spikes or not
   

  Transmits: SpikeEvent
   
  References:
   [1] T. Rumbell, S.Denham, T. Wennekers. A Spiking Self-Organizing Map
       Combining STDP, oscillations, and continuous learning

  FirstVersion: Aug 2014
  Author: Zhenzhong Wang 
  SeeAlso: synapsedict, tsodyks_synapse, static_synapse, stdp_synapse
*/

#include "connection_het_wd.h"
#include "archiving_node.h"
#include "generic_connector.h"
#include <cmath>

using namespace nest;

namespace mynest
{
  class STDPConnectionMulti : public ConnectionHetWD
  {

  public:
  /**
   * Default Constructor.
   * Sets default values for all parameters. Needed by GenericConnectorModel.
   */
  STDPConnectionMulti();

  /**
   * Copy constructor.
   * Needs to be defined properly in order for GenericConnector to work.
   */
  STDPConnectionMulti(const STDPConnectionMulti &);

  /**
   * Default Destructor.
   */
  ~STDPConnectionMulti() {}

  void check_connection(Node & s, Node & r, rport receptor_type, double_t t_lastspike);

  /**
   * Get all properties of this connection and put them into a dictionary.
   */
  void get_status(DictionaryDatum & d) const;

  /**
   * Set properties of this connection from the values given in dictionary.
   */
  void set_status(const DictionaryDatum & d, ConnectorModel &cm);

  /**
   * Set properties of this connection from position p in the properties
   * array given in dictionary.
   */
  void set_status(const DictionaryDatum & d, nest::index p, ConnectorModel &cm);

  /**
   * Create new empty arrays for the properties of this connection in the given
   * dictionary. It is assumed that they are not existing before.
   */
  void initialize_property_arrays(DictionaryDatum & d) const;

  /**
   * Append properties of this connection to the given dictionary. If the
   * dictionary is empty, new arrays are created first.
   */
  void append_properties(DictionaryDatum & d) const;

  /**
   * Send an event to the receiver of this connection.
   * \param e The event to send
   * \param t_lastspike Point in time of last spike sent.
   * \param cp common properties of all synapses (empty).
   */
  void send(Event& e, double_t t_lastspike, const CommonSynapseProperties &cp);

  // overloaded for all supported event types
  using Connection::check_event;
  void check_event(SpikeEvent&) {}

 private:

  //double_t facilitate_(double_t w, double_t kplus);
  //double_t depress_(double_t w, double_t kminus);
  double_t learn_(double_t w, double_t dt);

  // data members of each connection
  double_t Aplus_;
  double_t Aneg_;
  double_t tplus_;
  double_t tneg_;
  double_t Wmax_;
  double_t Esyn_;
  bool EmitSpk_;

  };

inline
double_t STDPConnectionMulti::learn_(double_t w, double_t dt)
{
    double_t wd,td,nw;
    if(dt>0)
    {
        wd = std::exp(-w) * Aplus_;
        td = std::pow(1.0 - 1.0/tplus_,dt);
    }
    else
    {
        wd = -w * Aneg_;
        td = std::pow(1.0 - 1.0/tneg_,-dt);
    }
    nw = w + wd * td;
    if(nw < 0.0){
        return 0.0;
    }
    else if(nw > Wmax_){
        return Wmax_;
    }
    else{
        return nw;
    }
}

//inline
//double_t STDPConnectionMulti::facilitate_(double_t w, double_t kplus)
//{
  //double_t norm_w = (w / Wmax_) + (lambda_ * std::pow(1.0 - (w/Wmax_), mu_plus_) * kplus);
  //return norm_w < 1.0 ? norm_w * Wmax_ : Wmax_;
//}

//inline 
//double_t STDPConnectionMulti::depress_(double_t w, double_t kminus)
//{
  //double_t norm_w = (w / Wmax_) - (alpha_ * lambda_ * std::pow(w/Wmax_, mu_minus_) * kminus);
  //return norm_w > 0.0 ? norm_w * Wmax_ : 0.0;
//}


inline 
void STDPConnectionMulti::check_connection(Node & s, Node & r, rport receptor_type, double_t t_lastspike)
{
  ConnectionHetWD::check_connection(s, r, receptor_type, t_lastspike);

  // For a new synapse, t_lastspike contains the point in time of the last spike.
  // So we initially read the history(t_last_spike - dendritic_delay, ...,  T_spike-dendritic_delay]
  // which increases the access counter for these entries.
  // At registration, all entries' access counters of history[0, ..., t_last_spike - dendritic_delay] will be 
  // incremented by the following call to Archiving_Node::register_stdp_connection().
  // See bug #218 for details.
  r.register_stdp_connection(t_lastspike - Time(Time::step(delay_)).get_ms());
}

/**
 * Send an event to the receiver of this connection.
 * \param e The event to send
 * \param p The port under which this connection is stored in the Connector.
 * \param t_lastspike Time point of last spike emitted
 */
inline
void STDPConnectionMulti::send(Event& e, double_t t_lastspike, const CommonSynapseProperties &)
{
  // synapse STDP depressing/facilitation dynamics

  double_t t_spike = e.get_stamp().get_ms();
  // t_lastspike_ = 0 initially
  double_t dendritic_delay = Time(Time::step(delay_)).get_ms();

  //get spike history in relevant range (t1, t2] from post-synaptic neuron
  std::deque<histentry>::iterator start;
  std::deque<histentry>::iterator finish;

  // For a new synapse, t_lastspike contains the point in time of the last spike.
  // So we initially read the history(t_last_spike - dendritic_delay, ...,  T_spike-dendritic_delay]
  // which increases the access counter for these entries.
  // At registration, all entries' access counters of history[0, ..., t_last_spike - dendritic_delay] have been 
  // incremented by Archiving_Node::register_stdp_connection(). See bug #218 for details.
  target_->get_history(t_lastspike - dendritic_delay, t_spike - dendritic_delay,
                         &start, &finish);
  //facilitation due to post-synaptic spikes since last pre-synaptic spike
  double_t minus_dt;
  while (start != finish)
  {
    minus_dt = t_lastspike - (start->t_ + dendritic_delay);
    ++start;
    weight_ = learn_(weight_, minus_dt);
  }

  if(EmitSpk_)
  {
    e.set_receiver(*target_);
    e.set_weight(weight_ * Esyn_);
    e.set_delay(delay_);
    e.set_rport(rport_);
    e();
  }
}

} // of namespace nest

#endif // of #ifndef STDP_CONNECTION_ALPHA_H
