; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(declare-fun ipv4.dstAddr () (_ BitVec 32))
(declare-fun ipv4.$valid$ () Bool)
(assert
 (let (($x134 (= standard_metadata.ingress_port (_ bv1 9))))
 (and (and (distinct standard_metadata.ingress_port (_ bv511 9)) true) (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x134))))
(assert
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let (($x73 (and $x69 (not (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let ((?x85 (ite $x71 ?x84 (ite (and $x42 $x73) (_ bv511 9) standard_metadata.egress_spec))))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 ?x85)))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (or $x41 (or (or false (= ?x121 (_ bv0 9))) (= ?x121 (_ bv1 9)))))))))))))))))))))))
(assert
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x96 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))) 3 (ite (and $x42 $x59) 2 (- 1)))))
 (let ((?x108 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))) 0 ?x96)))
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x128 (ite ipv4.$valid$ (ite $x60 1 ?x108) (- 1))))
 (let (($x127 (ite ipv4.$valid$ $x42 false)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let ((?x77 (ite (and $x42 (and $x69 (not $x59))) (_ bv511 9) standard_metadata.egress_spec)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 (ite $x71 ?x84 ?x77))))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (and (and (not $x41) $x127) (= ?x128 (- 1)))))))))))))))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(declare-fun ipv4.dstAddr () (_ BitVec 32))
(declare-fun ipv4.$valid$ () Bool)
(assert
 (let (($x134 (= standard_metadata.ingress_port (_ bv1 9))))
 (and (and (distinct standard_metadata.ingress_port (_ bv511 9)) true) (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x134))))
(assert
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let (($x73 (and $x69 (not (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let ((?x85 (ite $x71 ?x84 (ite (and $x42 $x73) (_ bv511 9) standard_metadata.egress_spec))))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 ?x85)))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (or $x41 (or (or false (= ?x121 (_ bv0 9))) (= ?x121 (_ bv1 9)))))))))))))))))))))))
(assert
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x96 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))) 3 (ite (and $x42 $x59) 2 (- 1)))))
 (let ((?x108 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))) 0 ?x96)))
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x128 (ite ipv4.$valid$ (ite $x60 1 ?x108) (- 1))))
 (let (($x127 (ite ipv4.$valid$ $x42 false)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let ((?x77 (ite (and $x42 (and $x69 (not $x59))) (_ bv511 9) standard_metadata.egress_spec)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 (ite $x71 ?x84 ?x77))))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (let (($x252 (and (not $x41) $x127)))
 (and $x252 (= ?x128 0)))))))))))))))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(declare-fun ipv4.dstAddr () (_ BitVec 32))
(declare-fun ipv4.$valid$ () Bool)
(assert
 (let (($x134 (= standard_metadata.ingress_port (_ bv1 9))))
 (and (and (distinct standard_metadata.ingress_port (_ bv511 9)) true) (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x134))))
(assert
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let (($x73 (and $x69 (not (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let ((?x85 (ite $x71 ?x84 (ite (and $x42 $x73) (_ bv511 9) standard_metadata.egress_spec))))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 ?x85)))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (or $x41 (or (or false (= ?x121 (_ bv0 9))) (= ?x121 (_ bv1 9)))))))))))))))))))))))
(assert
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x96 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))) 3 (ite (and $x42 $x59) 2 (- 1)))))
 (let ((?x108 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))) 0 ?x96)))
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x128 (ite ipv4.$valid$ (ite $x60 1 ?x108) (- 1))))
 (let (($x127 (ite ipv4.$valid$ $x42 false)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let ((?x77 (ite (and $x42 (and $x69 (not $x59))) (_ bv511 9) standard_metadata.egress_spec)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 (ite $x71 ?x84 ?x77))))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (and (and (not $x41) $x127) (= ?x128 1))))))))))))))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(declare-fun ipv4.dstAddr () (_ BitVec 32))
(declare-fun ipv4.$valid$ () Bool)
(assert
 (let (($x134 (= standard_metadata.ingress_port (_ bv1 9))))
 (and (and (distinct standard_metadata.ingress_port (_ bv511 9)) true) (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x134))))
(assert
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let (($x73 (and $x69 (not (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let ((?x85 (ite $x71 ?x84 (ite (and $x42 $x73) (_ bv511 9) standard_metadata.egress_spec))))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 ?x85)))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (or $x41 (or (or false (= ?x121 (_ bv0 9))) (= ?x121 (_ bv1 9)))))))))))))))))))))))
(assert
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x96 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))) 3 (ite (and $x42 $x59) 2 (- 1)))))
 (let ((?x108 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))) 0 ?x96)))
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x128 (ite ipv4.$valid$ (ite $x60 1 ?x108) (- 1))))
 (let (($x127 (ite ipv4.$valid$ $x42 false)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let ((?x77 (ite (and $x42 (and $x69 (not $x59))) (_ bv511 9) standard_metadata.egress_spec)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 (ite $x71 ?x84 ?x77))))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (and (and (not $x41) $x127) (= ?x128 2))))))))))))))))))))))))
(check-sat)

; 
(set-info :status unknown)
(declare-fun standard_metadata.ingress_port () (_ BitVec 9))
(declare-fun standard_metadata.egress_spec () (_ BitVec 9))
(declare-fun ipv4.dstAddr () (_ BitVec 32))
(declare-fun ipv4.$valid$ () Bool)
(assert
 (let (($x134 (= standard_metadata.ingress_port (_ bv1 9))))
 (and (and (distinct standard_metadata.ingress_port (_ bv511 9)) true) (or (or false (= standard_metadata.ingress_port (_ bv0 9))) $x134))))
(assert
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let (($x73 (and $x69 (not (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let ((?x85 (ite $x71 ?x84 (ite (and $x42 $x73) (_ bv511 9) standard_metadata.egress_spec))))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 ?x85)))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (or $x41 (or (or false (= ?x121 (_ bv0 9))) (= ?x121 (_ bv1 9)))))))))))))))))))))))
(assert
 (let (($x59 (and true (= ((_ extract 31 24) ipv4.dstAddr) ((_ extract 31 24) (_ bv167772160 32))))))
 (let (($x42 (and true ipv4.$valid$)))
 (let ((?x96 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))) 3 (ite (and $x42 $x59) 2 (- 1)))))
 (let ((?x108 (ite (and $x42 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))) 0 ?x96)))
 (let (($x45 (= ipv4.dstAddr (_ bv168427520 32))))
 (let (($x46 (and true $x45)))
 (let (($x60 (and $x42 $x46)))
 (let ((?x128 (ite ipv4.$valid$ (ite $x60 1 ?x108) (- 1))))
 (let (($x127 (ite ipv4.$valid$ $x42 false)))
 (let (($x61 (not $x46)))
 (let (($x65 (and $x61 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))))
 (let (($x69 (and $x65 (not (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))))
 (let ((?x77 (ite (and $x42 (and $x69 (not $x59))) (_ bv511 9) standard_metadata.egress_spec)))
 (let ((?x84 (concat (_ bv0 8) (_ bv1 1))))
 (let (($x71 (and (and $x42 $x69) $x59)))
 (let (($x54 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv336855040 32))))))
 (let (($x67 (and (and $x42 $x65) $x54)))
 (let (($x50 (and true (= ((_ extract 31 16) ipv4.dstAddr) ((_ extract 31 16) (_ bv168427520 32))))))
 (let (($x63 (and (and $x42 $x61) $x50)))
 (let ((?x121 (ite $x60 ?x84 (ite $x63 (concat (_ bv0 8) (_ bv0 1)) (ite $x67 ?x84 (ite $x71 ?x84 ?x77))))))
 (let (($x41 (= ?x121 (_ bv511 9))))
 (and (and (not $x41) $x127) (= ?x128 3))))))))))))))))))))))))
(check-sat)

