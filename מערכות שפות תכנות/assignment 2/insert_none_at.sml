(* helper function from part a *)
(* insert_at gets a value x, an index n and a list l *)
(* it returns a new list where x is at place n *)

(* if n is 0 we put x at the start of the list *)
fun insert_at x 0 l = x :: l
  (* this case can not happen because the list has at least n items *)
  (* we write it only so sml will not show a warning *)
  | insert_at x n [] = [x]
  (* keep the first item and put x in the rest of the list at place n - 1 *)
  | insert_at x n (h :: t) = h :: insert_at x (n - 1) t;

(* insert_none_at gets an index n and a list of strings l *)
(* it uses insert_at to put the string "None" at place n *)
fun insert_none_at n l = insert_at "None" n l;
