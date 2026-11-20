(* the type from the question for logic expressions *)
datatype expr =
    Var of string
  | Not of expr
  | And of expr * expr
  | Or of expr * expr;

(* helper: lookup gets a var name and a list of (name, value) pairs *)
(* it returns the value of this name *)
(* we write string so sml knows the type of the names *)
fun lookup (name : string) [] = false
  | lookup name ((v, b) :: rest) =
      if name = v then b else lookup name rest;

(* helper: eval gets a list of (name, value) pairs and an expression *)
(* it returns true or false for the expression with these values *)
fun eval env (Var x) = lookup x env
  | eval env (Not e) = not (eval env e)
  | eval env (And (e1, e2)) = eval env e1 andalso eval env e2
  | eval env (Or (e1, e2)) = eval env e1 orelse eval env e2;

(* helper: rows gets a list of var names and a list acc *)
(* it returns all the ways to give true or false to the vars *)
(* for the first var we first take true and then false *)
(* each new pair goes to the start of acc, so the last var is first *)
fun rows [] acc = [acc]
  | rows (v :: vs) acc =
      rows vs ((v, true) :: acc) @ rows vs ((v, false) :: acc);

(* table gets a list of var names and an expression *)
(* for every row we keep the values of the vars and the result *)
fun table vars e = map (fn env => (env, eval env e)) (rows vars []);
