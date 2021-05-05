; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x54 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x54)))
(assert
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x33 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv1 1))))))
 (let (($x24 (not false)))
 (let (($x37 (and (and $x24 (not (and true (= standard_metadata.ingress_port ?x27)))) $x33)))
 (let ((?x31 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x29 (and true (= standard_metadata.ingress_port ?x27))))
 (let (($x34 (and $x24 $x29)))
 (let ((?x48 (ite $x34 ?x31 (ite $x37 ?x27 standard_metadata.egress_spec))))
 (or (or (= ?x48 (_ bv455 9)) (= ?x48 (_ bv0 9))) (= ?x48 (_ bv1 9))))))))))))
(assert
 (let (($x33 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv1 1))))))
 (let (($x24 (not false)))
 (let (($x29 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv0 1))))))
 (let (($x34 (and $x24 $x29)))
 (let ((?x47 (ite $x34 0 (ite (and $x24 $x33) 1 (- 1)))))
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let ((?x45 (ite (and (and $x24 (not $x29)) $x33) ?x27 standard_metadata.egress_spec)))
 (let ((?x31 (concat (_ bv0 8) (_ bv1 1))))
 (let ((?x48 (ite $x34 ?x31 ?x45)))
 (let (($x39 (= ?x48 (_ bv455 9))))
 (and (and (not $x39) $x24) (= ?x47 (- 1))))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x54 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x54)))
(assert
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x33 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv1 1))))))
 (let (($x24 (not false)))
 (let (($x37 (and (and $x24 (not (and true (= standard_metadata.ingress_port ?x27)))) $x33)))
 (let ((?x31 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x29 (and true (= standard_metadata.ingress_port ?x27))))
 (let (($x34 (and $x24 $x29)))
 (let ((?x48 (ite $x34 ?x31 (ite $x37 ?x27 standard_metadata.egress_spec))))
 (or (or (= ?x48 (_ bv455 9)) (= ?x48 (_ bv0 9))) (= ?x48 (_ bv1 9))))))))))))
(assert
 (let (($x33 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv1 1))))))
 (let (($x24 (not false)))
 (let (($x29 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv0 1))))))
 (let (($x34 (and $x24 $x29)))
 (let ((?x47 (ite $x34 0 (ite (and $x24 $x33) 1 (- 1)))))
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let ((?x45 (ite (and (and $x24 (not $x29)) $x33) ?x27 standard_metadata.egress_spec)))
 (let ((?x31 (concat (_ bv0 8) (_ bv1 1))))
 (let ((?x48 (ite $x34 ?x31 ?x45)))
 (let (($x39 (= ?x48 (_ bv455 9))))
 (let (($x106 (and (not $x39) $x24)))
 (and $x106 (= ?x47 0))))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(assert
 (let (($x54 (= standard_metadata.ingress_port (_ bv1 9))))
 (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x54)))
(assert
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let (($x33 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv1 1))))))
 (let (($x24 (not false)))
 (let (($x37 (and (and $x24 (not (and true (= standard_metadata.ingress_port ?x27)))) $x33)))
 (let ((?x31 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x29 (and true (= standard_metadata.ingress_port ?x27))))
 (let (($x34 (and $x24 $x29)))
 (let ((?x48 (ite $x34 ?x31 (ite $x37 ?x27 standard_metadata.egress_spec))))
 (or (or (= ?x48 (_ bv455 9)) (= ?x48 (_ bv0 9))) (= ?x48 (_ bv1 9))))))))))))
(assert
 (let (($x33 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv1 1))))))
 (let (($x24 (not false)))
 (let (($x29 (and true (= standard_metadata.ingress_port (concat (_ bv0 8) (_ bv0 1))))))
 (let (($x34 (and $x24 $x29)))
 (let ((?x47 (ite $x34 0 (ite (and $x24 $x33) 1 (- 1)))))
 (let ((?x27 (concat (_ bv0 8) (_ bv0 1))))
 (let ((?x45 (ite (and (and $x24 (not $x29)) $x33) ?x27 standard_metadata.egress_spec)))
 (let ((?x31 (concat (_ bv0 8) (_ bv1 1))))
 (let ((?x48 (ite $x34 ?x31 ?x45)))
 (let (($x39 (= ?x48 (_ bv455 9))))
 (and (and (not $x39) $x24) (= ?x47 1)))))))))))))
(check-sat)

