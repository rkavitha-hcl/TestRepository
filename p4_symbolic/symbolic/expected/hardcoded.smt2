; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x47 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x47)))
(assert
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x28 (= standard_metadata.ingress_port ?x27)))
 (let (($x24 (not false)))
 (let (($x30 (and $x24 $x28)))
 (let (($x31 (and $x24 (not $x28))))
 (let ((?x36 (ite $x31 ?x27 (ite $x30 (concat (_ bv0 8) (_ bv1 1)) standard_metadata.egress_spec))))
 (or (or (= ?x36 (_ bv455 9)) (= ?x36 (_ bv0 9))) (= ?x36 (_ bv1 9))))))))))
(assert
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x28 (= standard_metadata.ingress_port ?x27)))
 (let (($x24 (not false)))
 (let (($x30 (and $x24 $x28)))
 (let (($x37 (ite $x28 $x30 false)))
 (let (($x31 (and $x24 (not $x28))))
 (let ((?x36 (ite $x31 ?x27 (ite $x30 (concat (_ bv0 8) (_ bv1 1)) standard_metadata.egress_spec))))
 (let (($x40 (= ?x36 (_ bv455 9))))
 (and (and (not $x40) $x37) (= (- 1) (- 1))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x47 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x47)))
(assert
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x28 (= standard_metadata.ingress_port ?x27)))
 (let (($x24 (not false)))
 (let (($x30 (and $x24 $x28)))
 (let (($x31 (and $x24 (not $x28))))
 (let ((?x36 (ite $x31 ?x27 (ite $x30 (concat (_ bv0 8) (_ bv1 1)) standard_metadata.egress_spec))))
 (or (or (= ?x36 (_ bv455 9)) (= ?x36 (_ bv0 9))) (= ?x36 (_ bv1 9))))))))))
(assert
 (let (($x24 (not false)))
 (let (($x31 (and $x24 (not (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv0 1)))))))
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x28 (= standard_metadata.ingress_port ?x27)))
 (let (($x38 (ite $x28 false $x31)))
 (let ((?x36 (ite $x31 ?x27 (ite (and $x24 $x28) (concat (_ bv0 8) (_ bv1 1)) standard_metadata.egress_spec))))
 (let (($x40 (= ?x36 (_ bv455 9))))
 (and (and (not $x40) $x38) (= (- 1) (- 1)))))))))))
(check-sat)

