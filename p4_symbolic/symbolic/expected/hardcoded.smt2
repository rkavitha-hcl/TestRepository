(ingress) $got_cloned$: false
(ingress) scalars.$extracted$: false
(ingress) scalars.$valid$: false
(ingress) standard_metadata.$extracted$: false
(ingress) standard_metadata.$valid$: false
(ingress) standard_metadata._padding: standard_metadata._padding
(ingress) standard_metadata.checksum_error: standard_metadata.checksum_error
(ingress) standard_metadata.deq_qdepth: standard_metadata.deq_qdepth
(ingress) standard_metadata.deq_timedelta: standard_metadata.deq_timedelta
(ingress) standard_metadata.egress_global_timestamp: standard_metadata.egress_global_timestamp
(ingress) standard_metadata.egress_port: standard_metadata.egress_port
(ingress) standard_metadata.egress_rid: standard_metadata.egress_rid
(ingress) standard_metadata.egress_spec: standard_metadata.egress_spec
(ingress) standard_metadata.enq_qdepth: standard_metadata.enq_qdepth
(ingress) standard_metadata.enq_timestamp: standard_metadata.enq_timestamp
(ingress) standard_metadata.ingress_global_timestamp: standard_metadata.ingress_global_timestamp
(ingress) standard_metadata.ingress_port: standard_metadata.ingress_port
(ingress) standard_metadata.instance_type: standard_metadata.instance_type
(ingress) standard_metadata.mcast_grp: standard_metadata.mcast_grp
(ingress) standard_metadata.packet_length: standard_metadata.packet_length
(ingress) standard_metadata.parser_error: #x00000000
(ingress) standard_metadata.priority: standard_metadata.priority

(parsed) $got_cloned$: false
(parsed) scalars.$extracted$: false
(parsed) scalars.$valid$: false
(parsed) standard_metadata.$extracted$: false
(parsed) standard_metadata.$valid$: false
(parsed) standard_metadata._padding: standard_metadata._padding
(parsed) standard_metadata.checksum_error: standard_metadata.checksum_error
(parsed) standard_metadata.deq_qdepth: standard_metadata.deq_qdepth
(parsed) standard_metadata.deq_timedelta: standard_metadata.deq_timedelta
(parsed) standard_metadata.egress_global_timestamp: standard_metadata.egress_global_timestamp
(parsed) standard_metadata.egress_port: standard_metadata.egress_port
(parsed) standard_metadata.egress_rid: standard_metadata.egress_rid
(parsed) standard_metadata.egress_spec: standard_metadata.egress_spec
(parsed) standard_metadata.enq_qdepth: standard_metadata.enq_qdepth
(parsed) standard_metadata.enq_timestamp: standard_metadata.enq_timestamp
(parsed) standard_metadata.ingress_global_timestamp: standard_metadata.ingress_global_timestamp
(parsed) standard_metadata.ingress_port: standard_metadata.ingress_port
(parsed) standard_metadata.instance_type: standard_metadata.instance_type
(parsed) standard_metadata.mcast_grp: standard_metadata.mcast_grp
(parsed) standard_metadata.packet_length: standard_metadata.packet_length
(parsed) standard_metadata.parser_error: (ite (and true (not true)) #x00000002 #x00000000)
(parsed) standard_metadata.priority: standard_metadata.priority

(egress) $got_cloned$: false
(egress) scalars.$extracted$: false
(egress) scalars.$valid$: false
(egress) standard_metadata.$extracted$: false
(egress) standard_metadata.$valid$: false
(egress) standard_metadata._padding: standard_metadata._padding
(egress) standard_metadata.checksum_error: standard_metadata.checksum_error
(egress) standard_metadata.deq_qdepth: standard_metadata.deq_qdepth
(egress) standard_metadata.deq_timedelta: standard_metadata.deq_timedelta
(egress) standard_metadata.egress_global_timestamp: standard_metadata.egress_global_timestamp
(egress) standard_metadata.egress_port: (let ((a!1 (and true (not (= standard_metadata.ingress_port (concat #x00 #b0)))))
      (a!2 (ite (and true (= standard_metadata.ingress_port (concat #x00 #b0)))
                (concat #x00 #b1)
                standard_metadata.egress_spec)))
(let ((a!3 (not (= (ite a!1 (concat #x00 #b0) a!2) #b111111111))))
  (ite a!3 (ite a!1 (concat #x00 #b0) a!2) standard_metadata.egress_port)))
(egress) standard_metadata.egress_rid: standard_metadata.egress_rid
(egress) standard_metadata.egress_spec: (let ((a!1 (and true (not (= standard_metadata.ingress_port (concat #x00 #b0)))))
      (a!2 (ite (and true (= standard_metadata.ingress_port (concat #x00 #b0)))
                (concat #x00 #b1)
                standard_metadata.egress_spec)))
  (ite a!1 (concat #x00 #b0) a!2))
(egress) standard_metadata.enq_qdepth: standard_metadata.enq_qdepth
(egress) standard_metadata.enq_timestamp: standard_metadata.enq_timestamp
(egress) standard_metadata.ingress_global_timestamp: standard_metadata.ingress_global_timestamp
(egress) standard_metadata.ingress_port: standard_metadata.ingress_port
(egress) standard_metadata.instance_type: standard_metadata.instance_type
(egress) standard_metadata.mcast_grp: standard_metadata.mcast_grp
(egress) standard_metadata.packet_length: standard_metadata.packet_length
(egress) standard_metadata.parser_error: (ite (and true (not true)) #x00000002 #x00000000)
(egress) standard_metadata.priority: standard_metadata.priority

(solver constraints)
; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x50 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x50)))
(assert
 (let ((?x28 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x29 (= standard_metadata.ingress_port ?x28)))
 (let (($x31 (and true $x29)))
 (let (($x32 (and true (not $x29))))
 (let ((?x37 (ite $x32 ?x28 (ite $x31 (concat (_ bv0 8) (_ bv1 1)) standard_metadata.egress_spec))))
 (let (($x53 (or (or false (= ?x37 (_ bv0 9))) (= ?x37 (_ bv1 9)))))
 (let (($x41 (= ?x37 (_ bv511 9))))
 (or $x41 $x53)))))))))
(check-sat)
