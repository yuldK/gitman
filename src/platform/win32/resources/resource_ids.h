#pragma once

// gitman 자신의 resource ID다. 101~104는 luil의 .rc 조각이 쓰고, 이후 gitman
// resource는 106부터 정의한다 (ADR-007 결정 7).
#define IDI_GITMAN 105

// luil 조각의 ID를 gitman 코드가 읽기 위한 거울이다. luil이 같은 값으로 resource를
// 싣는다. gitman의 embedded_assets가 luil로 옮겨 가면(C2) 함께 지운다.
#define IDR_CODICONS_FONT 101
#define IDR_CODICONS_LICENSE 102
#define IDR_CODICONS_LICENSE_CODE 103
#define IDR_THIRD_PARTY_NOTICES 104
