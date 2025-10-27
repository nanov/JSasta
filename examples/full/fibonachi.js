const N = 40;


let n1 = 0
let n2 = 1
let nextTerm: int;


for (let i = 1; i <= N; i++) {
    console.log(n1);
    nextTerm = n1 + n2;
    n1 = n2;
    n2 = nextTerm;
}
