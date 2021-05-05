; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x35 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x35)))
(assert
 (let (($x24 (not false)))
 (let ((?x26 (ite $x24 standard_metadata.ingress_port standard_metadata.egress_spec)))
 (or (or (= ?x26 (_ bv455 9)) (= ?x26 (_ bv0 9))) (= ?x26 (_ bv1 9))))))
(assert
 (let (($x24 (not false)))
 (let ((?x26 (ite $x24 standard_metadata.ingress_port standard_metadata.egress_spec)))
 (let (($x28 (= ?x26 (_ bv455 9))))
 (and (and (not $x28) $x24) (= (- 1) (- 1)))))))
(check-sat)

